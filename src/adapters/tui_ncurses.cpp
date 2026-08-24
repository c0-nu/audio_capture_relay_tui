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
                } else if (ch == 'p' || ch == 'P' || ch == ' ') {
                    st.paused.store(!st.paused.load());
                }
                ch = getch();
            }
        }

        void draw_line(int row, int cols, const std::string& text) {
            mvaddstr(row, 0, shorten(text, cols - 1).c_str());
        }

        void draw_header(const SharedState& st, int cols) {
            attron(A_BOLD);
            draw_line(0, cols, "AudioCaptureRelay TUI  capture -> application playback");
            attroff(A_BOLD);

            std::string kind = st.source_is_monitor ? "MONITOR / output-capture" : "INPUT";
            draw_line(1, cols, "Source: " + st.source_description + "  [" + kind + "]");
            draw_line(2, cols, "Name:   " + st.source_name);
        }

        void draw_levels(SharedState& st, const RelayConfig& cfg, int cols) {
            std::ostringstream status;
            status << "Volume: " << std::fixed << std::setprecision(0) << (st.volume.load() * 100.0f) << "%"
            << (st.muted.load() ? " [MUTED]" : "")
            << (st.paused.load() ? " [PAUSED]" : "")
            << " | latency " << cfg.latency_ms << "ms"
            << " | chunk " << cfg.chunk_ms << "ms"
            << " | frames " << st.frames_captured.load()
            << " | errors " << st.errors.load();
            draw_line(4, cols, status.str());

            int meter_width = std::max(8, std::min(44, cols - 20));
            draw_line(5, cols, "L peak " + meter_bar(st.peak_l.load(), meter_width) + " rms " + meter_bar(st.rms_l.load(), meter_width));
            draw_line(6, cols, "R peak " + meter_bar(st.peak_r.load(), meter_width) + " rms " + meter_bar(st.rms_r.load(), meter_width));

            float clips = st.clip_ratio.load();
            if (clips > 0.0f) {
                if (has_colors()) attron(COLOR_PAIR(COLOR_ALERT) | A_BOLD);
                std::ostringstream c;
                c << "CLIPPING risk: " << std::fixed << std::setprecision(1) << (clips * 100.0f) << "% of recent frames";
                draw_line(7, cols, c.str());
                if (has_colors()) attroff(COLOR_PAIR(COLOR_ALERT) | A_BOLD);
            } else {
                if (has_colors()) attron(COLOR_PAIR(COLOR_OK));
                mvaddstr(7, 0, "CLIPPING risk: none");
                if (has_colors()) attroff(COLOR_PAIR(COLOR_OK));
            }
        }

        void draw_buffer_line(SharedState& st, const RelayConfig& cfg, int cols) {
            std::int64_t total_frames = st.smoothed_total_frames.load();
            std::int64_t out_frames = st.downstream_frames.load();
            std::int64_t ring_frames = static_cast<std::int64_t>(st.ring.frames_buffered());
            std::int64_t drift_ms = st.drift_ms.load();

            std::ostringstream buf_line;
            buf_line << "Latency: " << std::fixed << std::setprecision(0) << frames_to_ms(static_cast<double>(total_frames))
            << "ms / target " << cfg.latency_ms << "ms"
            << "  (ring " << frames_to_ms(static_cast<double>(ring_frames))
            << " + out " << frames_to_ms(static_cast<double>(out_frames)) << ")"
            << " drift " << (drift_ms >= 0 ? "+" : "") << drift_ms << "ms"
            << (st.reserve_hold.load() ? " [hold]" : "")
            << " | underruns " << st.underruns.load()
            << " | overflow trims " << st.overflow_trims.load();
            draw_line(8, cols, buf_line.str());
        }

        void draw_waveform(SharedState& st, int rows, int cols) {
            if (!st.waveform_enabled.load()) {
                mvaddstr(9, 0, "Waveform hidden. Press w to show.");
                return;
            }

            if (has_colors()) attron(COLOR_PAIR(COLOR_INFO));
            draw_line(9, cols, "Waveform: Unicode Braille, mono mix, recent history");
            if (has_colors()) attroff(COLOR_PAIR(COLOR_INFO));

            std::vector<WaveBucket> buckets = st.wave.snapshot();

            int wave_top = 10;
            int wave_rows = std::max(1, rows - wave_top - 2);
            int wave_cols = std::max(1, cols - 1);

            auto lines = make_braille_waveform(buckets, wave_cols, wave_rows);
            for (int i = 0; i < static_cast<int>(lines.size()) && wave_top + i < rows - 1; ++i) {
                mvaddstr(wave_top + i, 0, lines[i].c_str());
            }
        }

    } // namespace

    void run_tui(SharedState& st, const RelayConfig& cfg) {
        init_screen();

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

            draw_header(st, cols);
            draw_levels(st, cfg, cols);
            draw_line(rows - 1, cols, "q quit | +/- volume | m mute | w waveform | p pause");
            draw_buffer_line(st, cfg, cols);
            draw_waveform(st, rows, cols);

            refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        endwin();
    }

} // namespace acr
