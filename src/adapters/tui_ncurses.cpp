#include "adapters/tui_ncurses.h"

#include "domain/text_util.h"
#include "domain/waveform.h"

#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <locale.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace acr {

    namespace {

        constexpr int COLOR_OK = 1;
        constexpr int COLOR_WARN = 2;
        constexpr int COLOR_ALERT = 3;
        constexpr int COLOR_INFO = 4;

        void init_screen() {
            setlocale(LC_ALL, "");

            initscr();
            cbreak();
            noecho();
            nodelay(stdscr, TRUE);
            keypad(stdscr, TRUE);
            curs_set(0);

            if (has_colors()) {
                start_color();
                use_default_colors();
                init_pair(COLOR_OK, COLOR_GREEN, -1);
                init_pair(COLOR_WARN, COLOR_YELLOW, -1);
                init_pair(COLOR_ALERT, COLOR_RED, -1);
                init_pair(COLOR_INFO, COLOR_CYAN, -1);
            }
        }

        // 溜まっているキーをすべて処理する。
        void handle_keys(SharedState& st) {
            int ch = getch();
            while (ch != ERR) {
                if (ch == 'q' || ch == 'Q') {
                    st.request_stop();
                } else if (ch == '+' || ch == '=') {
                    st.volume.store(std::clamp(st.volume.load() + 0.05f, 0.0f, 4.0f));
                } else if (ch == '-' || ch == '_') {
                    st.volume.store(std::clamp(st.volume.load() - 0.05f, 0.0f, 4.0f));
                } else if (ch == 'm' || ch == 'M') {
                    st.muted.store(!st.muted.load());
                } else if (ch == 'w' || ch == 'W') {
                    st.waveform_enabled.store(!st.waveform_enabled.load());
                } else if (ch == 's' || ch == 'S') {
                    st.waveform_style.store(st.waveform_style.load() == WaveformStyle::Envelope
                                            ? WaveformStyle::Line
                                            : WaveformStyle::Envelope);
                } else if (ch == 'p' || ch == 'P' || ch == ' ') {
                    st.paused.store(!st.paused.load());
                }
                ch = getch();
            }
        }

        void draw_line(int row, int cols, const std::string& text) {
            mvaddstr(row, 0, shorten(text, cols - 1).c_str());
        }

        // 各 draw_* は「次に使える行」を返す。行番号を直書きしないので、
        // 行を増やしても以降がずれない。
        int draw_header(SharedState& st, int cols, int row) {
            attron(A_BOLD);
            draw_line(row++, cols, st.relay_enabled
                      ? "AudioCaptureRelay TUI  capture -> application playback"
                      : "AudioCaptureRelay TUI  capture -> visualize only (--no-relay)");
            attroff(A_BOLD);

            std::string kind = st.source_is_monitor ? "MONITOR / output-capture" : "INPUT";
            draw_line(row++, cols, "Source: " + st.source_description + "  [" + kind + "]");
            draw_line(row++, cols, "Name:   " + st.source_name);
            draw_line(row++, cols, "Output: " + (!st.relay_enabled ? std::string("(none: --no-relay)")
                                                 : st.sink_description.empty() ? std::string("(default sink)")
                                                 : st.sink_description));

            // エラーは stderr ではなくここに出す(TUI 中に stderr へ書くと画面が壊れる)。
            auto err = st.errors.snapshot();
            if (err.count > 0) {
                if (has_colors()) attron(COLOR_PAIR(COLOR_ALERT));
                std::ostringstream line;
                line << "Error:  " << err.message << "  (" << err.count << " total)";
                draw_line(row++, cols, line.str());
                if (has_colors()) attroff(COLOR_PAIR(COLOR_ALERT));
            }

            return row;
        }

        int draw_levels(SharedState& st, const RelayConfig& cfg, int cols, int row) {
            std::ostringstream status;
            if (st.relay_enabled) {
                status << "Volume: " << std::fixed << std::setprecision(0) << (st.volume.load() * 100.0f) << "%"
                << (st.muted.load() ? " [MUTED]" : "")
                << (st.paused.load() ? " [PAUSED]" : "")
                << " | latency " << cfg.latency_ms << "ms";
            } else {
                status << "Visualize only: nothing is played back";
            }
            status << " | chunk " << cfg.chunk_ms << "ms"
            << " | frames " << st.frames_captured.load()
            << " | errors " << st.errors.count();
            draw_line(row++, cols, status.str());

            int meter_width = std::max(8, std::min(44, cols - 20));
            draw_line(row++, cols, "L peak " + meter_bar(st.peak_l.load(), meter_width) + " rms " + meter_bar(st.rms_l.load(), meter_width));
            draw_line(row++, cols, "R peak " + meter_bar(st.peak_r.load(), meter_width) + " rms " + meter_bar(st.rms_r.load(), meter_width));

            float clips = st.clip_ratio.load();
            if (clips > 0.0f) {
                if (has_colors()) attron(COLOR_PAIR(COLOR_ALERT) | A_BOLD);
                std::ostringstream c;
                c << "CLIPPING risk: " << std::fixed << std::setprecision(1) << (clips * 100.0f) << "% of recent frames";
                draw_line(row++, cols, c.str());
                if (has_colors()) attroff(COLOR_PAIR(COLOR_ALERT) | A_BOLD);
            } else {
                if (has_colors()) attron(COLOR_PAIR(COLOR_OK));
                draw_line(row++, cols, "CLIPPING risk: none");
                if (has_colors()) attroff(COLOR_PAIR(COLOR_OK));
            }

            return row;
        }

        int draw_latency(SharedState& st, const RelayConfig& cfg, int cols, int row) {
            // 中継していなければリングもサーバ側キューも使っていない。0 の行を出さない。
            if (!st.relay_enabled) return row;

            std::int64_t total_frames = st.smoothed_total_frames.load();
            std::int64_t out_frames = st.downstream_frames.load();
            std::int64_t ring_frames = static_cast<std::int64_t>(st.ring.frames_buffered());
            std::int64_t drift_ms = st.drift_ms.load();

            std::ostringstream line;
            line << "Latency: " << std::fixed << std::setprecision(0) << frames_to_ms(static_cast<double>(total_frames))
            << "ms / target " << cfg.latency_ms << "ms";

            // 要求どおりに下げられない環境では、実際に狙っている水位も出す。
            if (st.raised_target.load()) {
                line << " (floor " << frames_to_ms(static_cast<double>(st.effective_target_frames.load())) << "ms)";
            }

            line << "  (ring " << frames_to_ms(static_cast<double>(ring_frames))
            << " + out " << frames_to_ms(static_cast<double>(out_frames)) << ")"
            << " drift " << (drift_ms >= 0 ? "+" : "") << drift_ms << "ms"
            << " | underruns " << st.underruns.load()
            << " | pads " << st.pads.load()
            << " | overflow trims " << st.overflow_trims.load();
            draw_line(row++, cols, line.str());

            return row;
        }

        void draw_waveform(SharedState& st,
                           int rows,
                           int cols,
                           int row,
                           std::vector<WaveBucket>& buckets,
                           WaveformRenderBuffer& render_buffer) {
            if (!st.waveform_enabled.load()) {
                draw_line(row, cols, "Waveform hidden. Press w to show.");
                return;
            }

            const WaveformStyle style = st.waveform_style.load();

            if (has_colors()) attron(COLOR_PAIR(COLOR_INFO));
            draw_line(row, cols, std::string("Waveform: Unicode Braille, mono mix, recent history  [")
                      + waveform_style_name(style) + "]");
            if (has_colors()) attroff(COLOR_PAIR(COLOR_INFO));

            st.wave.snapshot(buckets);

            int wave_top = row + 1;
            int wave_rows = std::max(1, rows - wave_top - 2);
            int wave_cols = std::max(1, cols - 1);

            render_braille_waveform(buckets, wave_cols, wave_rows, style, render_buffer);
            for (int i = 0; i < static_cast<int>(render_buffer.lines.size()) && wave_top + i < rows - 1; ++i) {
                mvaddstr(wave_top + i, 0, render_buffer.lines[static_cast<std::size_t>(i)].c_str());
            }
        }

    } // namespace

    void run_tui(SharedState& st, const RelayConfig& cfg) {
        init_screen();

        // 20fps の描画ループでヒープ確保を繰り返さないよう、呼び出し間で保持する。
        std::vector<WaveBucket> wave_buckets;
        WaveformRenderBuffer wave_render_buffer;

        while (st.is_running()) {
            handle_keys(st);

            erase();

            int rows = 0, cols = 0;
            getmaxyx(stdscr, rows, cols);

            if (rows < 10 || cols < 40) {
                mvaddstr(0, 0, "Terminal too small.");
                refresh();
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            int row = draw_header(st, cols, 0);
            ++row; // 空行
            row = draw_levels(st, cfg, cols, row);
            row = draw_latency(st, cfg, cols, row);
            draw_waveform(st, rows, cols, row, wave_buckets, wave_render_buffer);

            draw_line(rows - 1, cols, st.relay_enabled
                      ? "q quit | +/- volume | m mute | w waveform | s style | p pause"
                      : "q quit | w waveform | s style");

            refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        endwin();
    }

} // namespace acr
