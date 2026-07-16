#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <ncurses.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <locale.h>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

    constexpr int SAMPLE_RATE = 48000;
    constexpr int CHANNELS = 2;
    constexpr int BYTES_PER_SAMPLE = 2;
    constexpr int DEFAULT_CHUNK_MS = 20;
    constexpr int DEFAULT_LATENCY_MS = 120;
    constexpr size_t WAVE_HISTORY_SAMPLES = SAMPLE_RATE * 4; // mono samples

    std::atomic<bool> g_running{true};

    void on_signal(int) {
        g_running.store(false);
    }

    std::string safe(const char* s) {
        return s ? std::string{s} : std::string{};
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    std::string shorten(const std::string& s, int max_len) {
        if (max_len <= 0) return "";
        if ((int)s.size() <= max_len) return s;
        if (max_len <= 3) {
            size_t cut = 0;
            while (cut < s.size() && (int)cut < max_len) {
                unsigned char c = static_cast<unsigned char>(s[cut]);
                size_t step = 1;
                if ((c & 0xE0) == 0xC0) step = 2;
                else if ((c & 0xF0) == 0xE0) step = 3;
                else if ((c & 0xF8) == 0xF0) step = 4;
                if ((int)(cut + step) > max_len) break;
                cut += step;
            }
            return s.substr(0, cut);
        }

        int body_len = max_len - 3;
        size_t cut = 0;
        while (cut < s.size() && (int)cut < body_len) {
            unsigned char c = static_cast<unsigned char>(s[cut]);
            size_t step = 1;
            if ((c & 0xE0) == 0xC0) step = 2;
            else if ((c & 0xF0) == 0xE0) step = 3;
            else if ((c & 0xF8) == 0xF0) step = 4;
            if ((int)(cut + step) > body_len) break;
            cut += step;
        }
        return s.substr(0, cut) + "...";
    }

    struct SourceInfo {
        uint32_t index = 0;
        std::string name;
        std::string description;
        bool is_monitor = false;
        std::string monitor_of_sink_name;
        bool is_default = false;
    };

    class PulseSourceLister {
    public:
        bool query(std::string& error_message) {
            int error = 0;
            mainloop_ = pa_mainloop_new();
            if (!mainloop_) {
                error_message = "pa_mainloop_new failed";
                return false;
            }

            api_ = pa_mainloop_get_api(mainloop_);
            context_ = pa_context_new(api_, "AudioCaptureRelay Source Query");
            if (!context_) {
                error_message = "pa_context_new failed";
                cleanup();
                return false;
            }

            pa_context_set_state_callback(context_, &PulseSourceLister::context_state_cb, this);

            if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
                error_message = std::string("pa_context_connect failed: ") + pa_strerror(pa_context_errno(context_));
                cleanup();
                return false;
            }

            while (true) {
                if (pa_mainloop_iterate(mainloop_, 1, &error) < 0) {
                    error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                    cleanup();
                    return false;
                }

                auto state = pa_context_get_state(context_);
                if (state == PA_CONTEXT_READY) break;
                if (!PA_CONTEXT_IS_GOOD(state)) {
                    error_message = std::string("PulseAudio context failed: ") + pa_strerror(pa_context_errno(context_));
                    cleanup();
                    return false;
                }
            }

            if (!wait_operation(pa_context_get_server_info(context_, &PulseSourceLister::server_info_cb, this), error_message)) {
                cleanup();
                return false;
            }

            if (!wait_operation(pa_context_get_source_info_list(context_, &PulseSourceLister::source_info_cb, this), error_message)) {
                cleanup();
                return false;
            }

            for (auto& s : sources_) {
                s.is_default = (s.name == default_source_);
            }

            std::sort(sources_.begin(), sources_.end(), [](const SourceInfo& a, const SourceInfo& b) {
                if (a.is_default != b.is_default) return a.is_default > b.is_default;
                if (a.is_monitor != b.is_monitor) return a.is_monitor < b.is_monitor;
                return a.description < b.description;
            });

            cleanup();
            return true;
        }

        const std::vector<SourceInfo>& sources() const { return sources_; }

    private:
        pa_mainloop* mainloop_ = nullptr;
        pa_mainloop_api* api_ = nullptr;
        pa_context* context_ = nullptr;
        std::vector<SourceInfo> sources_;
        std::string default_source_;

        static void context_state_cb(pa_context*, void*) {}

        static void server_info_cb(pa_context*, const pa_server_info* info, void* userdata) {
            auto* self = static_cast<PulseSourceLister*>(userdata);
            if (info && info->default_source_name) {
                self->default_source_ = info->default_source_name;
            }
        }

        static void source_info_cb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
            if (eol > 0 || !info) return;
            auto* self = static_cast<PulseSourceLister*>(userdata);

            SourceInfo s;
            s.index = info->index;
            s.name = safe(info->name);
            s.description = safe(info->description);
            s.is_monitor = info->monitor_of_sink != PA_INVALID_INDEX;
            s.monitor_of_sink_name = safe(info->monitor_of_sink_name);
            self->sources_.push_back(std::move(s));
        }

        bool wait_operation(pa_operation* op, std::string& error_message) {
            if (!op) {
                error_message = std::string("PulseAudio operation failed: ") + pa_strerror(pa_context_errno(context_));
                return false;
            }

            int error = 0;
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                if (pa_mainloop_iterate(mainloop_, 1, &error) < 0) {
                    error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                    pa_operation_unref(op);
                    return false;
                }
            }

            pa_operation_unref(op);
            return true;
        }

        void cleanup() {
            if (context_) {
                pa_context_disconnect(context_);
                pa_context_unref(context_);
                context_ = nullptr;
            }
            if (mainloop_) {
                pa_mainloop_free(mainloop_);
                mainloop_ = nullptr;
            }
            api_ = nullptr;
        }
    };

    struct SharedState {
        std::mutex wave_mutex;
        std::deque<float> mono_history;

        std::atomic<float> volume{1.0f};
        std::atomic<bool> muted{false};
        std::atomic<bool> waveform_enabled{true};
        std::atomic<bool> paused{false};

        std::atomic<float> rms_l{0.0f};
        std::atomic<float> rms_r{0.0f};
        std::atomic<float> peak_l{0.0f};
        std::atomic<float> peak_r{0.0f};
        std::atomic<float> clip_ratio{0.0f};
        std::atomic<uint64_t> frames_captured{0};
        std::atomic<uint64_t> errors{0};

        std::string source_name;
        std::string source_description;
        bool source_is_monitor = false;
    };

    struct Options {
        bool list = false;
        bool tui = true;
        bool waveform = true;
        bool interactive_select = false;
        std::string source_arg;
        float volume = 1.0f;
        int latency_ms = DEFAULT_LATENCY_MS;
        int chunk_ms = DEFAULT_CHUNK_MS;
    };

    void print_usage(const char* argv0) {
        std::cout
        << "Usage:\n"
        << "  " << argv0 << " --list\n"
        << "  " << argv0 << " [--source NAME_OR_INDEX] [--volume 80] [--latency-ms 120] [--chunk-ms 20]\n"
        << "\n"
        << "Options:\n"
        << "  --list                 List capture sources.\n"
        << "  --source VALUE          Source index, exact name, or substring. Omit to use default source.\n"
        << "  --select               Choose source interactively before starting.\n"
        << "  --volume PERCENT       Software output volume. Default: 100.\n"
        << "  --latency-ms MS         Playback buffer target. Default: 120.\n"
        << "  --chunk-ms MS           Read/write chunk size. Default: 20.\n"
        << "  --no-tui               Print plain status instead of full-screen TUI.\n"
        << "  --no-waveform          Start with waveform hidden.\n"
        << "  --help                 Show this help.\n";
    }

    std::optional<Options> parse_args(int argc, char** argv) {
        Options opt;

        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];

            auto need_value = [&](const std::string& name) -> std::optional<std::string> {
                if (i + 1 >= argc) {
                    std::cerr << name << " requires a value\n";
                    return std::nullopt;
                }
                return std::string(argv[++i]);
            };

            if (a == "--help" || a == "-h") {
                print_usage(argv[0]);
                return std::nullopt;
            } else if (a == "--list") {
                opt.list = true;
            } else if (a == "--source" || a == "-s") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                opt.source_arg = *v;
            } else if (a == "--select") {
                opt.interactive_select = true;
            } else if (a == "--volume" || a == "-v") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                opt.volume = std::clamp(std::stof(*v) / 100.0f, 0.0f, 4.0f);
            } else if (a == "--latency-ms") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                opt.latency_ms = std::clamp(std::stoi(*v), 20, 2000);
            } else if (a == "--chunk-ms") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                opt.chunk_ms = std::clamp(std::stoi(*v), 5, 200);
            } else if (a == "--no-tui") {
                opt.tui = false;
            } else if (a == "--no-waveform") {
                opt.waveform = false;
            } else {
                std::cerr << "Unknown option: " << a << "\n";
                print_usage(argv[0]);
                return std::nullopt;
            }
        }

        if (!opt.tui) opt.waveform = false;
        return opt;
    }

    void print_sources(const std::vector<SourceInfo>& sources) {
        std::cout << "Capture sources:\n\n";
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& s = sources[i];

            std::cout << "[" << i << "] "
            << (s.is_default ? "* " : "  ")
            << s.description;

            if (s.is_monitor) std::cout << "  [MONITOR / output-capture]";
            else std::cout << "  [INPUT]";

            std::cout << "\n"
            << "    name: " << s.name << "\n";

            if (s.is_monitor && !s.monitor_of_sink_name.empty()) {
                std::cout << "    monitor of: " << s.monitor_of_sink_name << "\n";
            }

            std::cout << "\n";
        }
    }

    bool parse_index(const std::string& s, size_t& out) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try {
            out = static_cast<size_t>(std::stoull(s));
            return true;
        } catch (...) {
            return false;
        }
    }

    std::optional<SourceInfo> choose_source(
        const std::vector<SourceInfo>& sources,
        const std::string& source_arg,
        bool interactive
    ) {
        if (sources.empty()) return std::nullopt;

        if (interactive) {
            print_sources(sources);
            std::cout << "Select source number: " << std::flush;
            std::string line;
            std::getline(std::cin, line);
            size_t idx = 0;
            if (parse_index(line, idx) && idx < sources.size()) {
                return sources[idx];
            }
            std::cerr << "Invalid source index.\n";
            return std::nullopt;
        }

        if (source_arg.empty()) {
            auto it = std::find_if(sources.begin(), sources.end(), [](const SourceInfo& s) {
                return s.is_default;
            });
            if (it != sources.end()) return *it;
            return sources.front();
        }

        size_t idx = 0;
        if (parse_index(source_arg, idx) && idx < sources.size()) {
            return sources[idx];
        }

        auto exact = std::find_if(sources.begin(), sources.end(), [&](const SourceInfo& s) {
            return s.name == source_arg || s.description == source_arg;
        });
        if (exact != sources.end()) return *exact;

        std::string q = lower(source_arg);
        std::vector<SourceInfo> matches;
        for (const auto& s : sources) {
            std::string hay = lower(s.name + "\n" + s.description);
            if (hay.find(q) != std::string::npos) matches.push_back(s);
        }

        if (matches.size() == 1) return matches.front();

        if (matches.empty()) {
            std::cerr << "No source matched: " << source_arg << "\n";
        } else {
            std::cerr << "Source argument is ambiguous:\n";
            for (const auto& s : matches) {
                std::cerr << "  - " << s.description << " [" << s.name << "]\n";
            }
        }
        return std::nullopt;
    }

    void update_analysis(SharedState& st, const std::vector<int16_t>& input, size_t frames, float analysis_volume) {
        double sum_l = 0.0;
        double sum_r = 0.0;
        float peak_l = 0.0f;
        float peak_r = 0.0f;
        uint64_t clips = 0;

        std::vector<float> mono;
        mono.reserve(frames);

        for (size_t f = 0; f < frames; ++f) {
            float l = (input[f * 2 + 0] / 32768.0f) * analysis_volume;
            float r = (input[f * 2 + 1] / 32768.0f) * analysis_volume;

            float al = std::abs(l);
            float ar = std::abs(r);

            if (al >= 0.999f || ar >= 0.999f) clips++;

            peak_l = std::max(peak_l, std::min(al, 1.0f));
            peak_r = std::max(peak_r, std::min(ar, 1.0f));
            sum_l += l * l;
            sum_r += r * r;

            mono.push_back(std::clamp((l + r) * 0.5f, -1.0f, 1.0f));
        }

        st.rms_l.store(static_cast<float>(std::sqrt(sum_l / std::max<size_t>(1, frames))));
        st.rms_r.store(static_cast<float>(std::sqrt(sum_r / std::max<size_t>(1, frames))));
        st.peak_l.store(peak_l);
        st.peak_r.store(peak_r);
        st.clip_ratio.store(static_cast<float>(clips) / std::max<size_t>(1, frames));

        std::lock_guard<std::mutex> lk(st.wave_mutex);
        for (float v : mono) st.mono_history.push_back(v);
        while (st.mono_history.size() > WAVE_HISTORY_SAMPLES) st.mono_history.pop_front();
    }

    std::vector<std::string> make_braille_waveform(const std::vector<float>& samples, int cols, int rows) {
        if (cols <= 0 || rows <= 0) return {};

        constexpr unsigned char dot_map[4][2] = {
            {0x01, 0x08}, // dots 1,4
            {0x02, 0x10}, // dots 2,5
            {0x04, 0x20}, // dots 3,6
            {0x40, 0x80}  // dots 7,8
        };

        const int pixel_w = cols * 2;
        const int pixel_h = rows * 4;
        const int mid = pixel_h / 2;

        std::vector<std::vector<unsigned char>> cells(rows, std::vector<unsigned char>(cols, 0));

        if (samples.empty()) {
            return std::vector<std::string>(rows, std::string());
        }

        auto sample_at = [&](int x) -> float {
            if (pixel_w <= 1) return samples.back();
            double t = static_cast<double>(x) / static_cast<double>(pixel_w - 1);
            size_t idx = static_cast<size_t>(t * static_cast<double>(samples.size() - 1));
            idx = std::min(idx, samples.size() - 1);
            return std::clamp(samples[idx], -1.0f, 1.0f);
        };

        int prev_y = mid;
        for (int x = 0; x < pixel_w; ++x) {
            float v = sample_at(x);
            int y = static_cast<int>(std::lround((1.0f - (v + 1.0f) * 0.5f) * (pixel_h - 1)));
            y = std::clamp(y, 0, pixel_h - 1);

            int y0 = std::min(prev_y, y);
            int y1 = std::max(prev_y, y);
            if (x == 0) y0 = y1 = y;

            for (int yy = y0; yy <= y1; ++yy) {
                int cell_x = x / 2;
                int cell_y = yy / 4;
                int dot_x = x % 2;
                int dot_y = yy % 4;
                if (cell_x >= 0 && cell_x < cols && cell_y >= 0 && cell_y < rows) {
                    cells[cell_y][cell_x] |= dot_map[dot_y][dot_x];
                }
            }

            prev_y = y;
        }

        std::vector<std::string> lines;
        lines.reserve(rows);

        for (int y = 0; y < rows; ++y) {
            std::string line;
            line.reserve(cols * 3);

            for (int x = 0; x < cols; ++x) {
                uint32_t code = 0x2800u + cells[y][x];
                line.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                line.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                line.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }

            lines.push_back(std::move(line));
        }

        return lines;
    }

    std::string meter(float value, int width) {
        value = std::clamp(value, 0.0f, 1.0f);
        int filled = static_cast<int>(std::lround(value * width));
        std::string s;
        s.reserve(width + 2);
        s.push_back('[');
        for (int i = 0; i < width; ++i) s.push_back(i < filled ? '#' : '-');
        s.push_back(']');
        return s;
    }

    void draw_tui(SharedState& st, const Options& opt) {
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
            init_pair(1, COLOR_GREEN, -1);
            init_pair(2, COLOR_YELLOW, -1);
            init_pair(3, COLOR_RED, -1);
            init_pair(4, COLOR_CYAN, -1);
        }

        while (g_running.load()) {
            int ch = getch();
            while (ch != ERR) {
                if (ch == 'q' || ch == 'Q') {
                    g_running.store(false);
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

            erase();

            int rows = 0, cols = 0;
            getmaxyx(stdscr, rows, cols);

            if (rows < 10 || cols < 40) {
                mvaddstr(0, 0, "Terminal too small.");
                refresh();
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }

            attron(A_BOLD);
            mvaddstr(0, 0, shorten("AudioCaptureRelay TUI  capture -> application playback", cols - 1).c_str());
            attroff(A_BOLD);

            std::string kind = st.source_is_monitor ? "MONITOR / output-capture" : "INPUT";
            mvaddstr(1, 0, shorten("Source: " + st.source_description + "  [" + kind + "]", cols - 1).c_str());
            mvaddstr(2, 0, shorten("Name:   " + st.source_name, cols - 1).c_str());

            float vol = st.volume.load();
            bool muted = st.muted.load();
            bool paused = st.paused.load();
            float rms_l = st.rms_l.load();
            float rms_r = st.rms_r.load();
            float peak_l = st.peak_l.load();
            float peak_r = st.peak_r.load();
            float clips = st.clip_ratio.load();
            uint64_t frames = st.frames_captured.load();
            uint64_t errors = st.errors.load();

            std::ostringstream status;
            status << "Volume: " << std::fixed << std::setprecision(0) << (vol * 100.0f) << "%"
            << (muted ? " [MUTED]" : "")
            << (paused ? " [PAUSED]" : "")
            << " | latency " << opt.latency_ms << "ms"
            << " | chunk " << opt.chunk_ms << "ms"
            << " | frames " << frames
            << " | errors " << errors;
            mvaddstr(4, 0, shorten(status.str(), cols - 1).c_str());

            int meter_width = std::max(8, std::min(44, cols - 20));
            mvaddstr(5, 0, shorten("L peak " + meter(peak_l, meter_width) + " rms " + meter(rms_l, meter_width), cols - 1).c_str());
            mvaddstr(6, 0, shorten("R peak " + meter(peak_r, meter_width) + " rms " + meter(rms_r, meter_width), cols - 1).c_str());

            if (clips > 0.0f) {
                if (has_colors()) attron(COLOR_PAIR(3) | A_BOLD);
                std::ostringstream c;
                c << "CLIPPING risk: " << std::fixed << std::setprecision(1) << (clips * 100.0f) << "% of recent frames";
                mvaddstr(7, 0, shorten(c.str(), cols - 1).c_str());
                if (has_colors()) attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                if (has_colors()) attron(COLOR_PAIR(1));
                mvaddstr(7, 0, "CLIPPING risk: none");
                if (has_colors()) attroff(COLOR_PAIR(1));
            }

            mvaddstr(rows - 1, 0, shorten("q quit | +/- volume | m mute | w waveform | p pause", cols - 1).c_str());

            if (st.waveform_enabled.load()) {
                if (has_colors()) attron(COLOR_PAIR(4));
                mvaddstr(8, 0, shorten("Waveform: Unicode Braille, mono mix, recent history", cols - 1).c_str());
                if (has_colors()) attroff(COLOR_PAIR(4));

                std::vector<float> samples;
                {
                    std::lock_guard<std::mutex> lk(st.wave_mutex);
                    samples.assign(st.mono_history.begin(), st.mono_history.end());
                }

                int wave_top = 9;
                int wave_rows = std::max(1, rows - wave_top - 2);
                int wave_cols = std::max(1, cols - 1);

                auto lines = make_braille_waveform(samples, wave_cols, wave_rows);
                for (int i = 0; i < (int)lines.size() && wave_top + i < rows - 1; ++i) {
                    mvaddstr(wave_top + i, 0, lines[i].c_str());
                }
            } else {
                mvaddstr(8, 0, "Waveform hidden. Press w to show.");
            }

            refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        endwin();
    }

    void audio_thread(SharedState& st, const Options& opt) {
        pa_sample_spec ss{};
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = SAMPLE_RATE;
        ss.channels = CHANNELS;

        const int bytes_per_second = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE;
        const int chunk_frames = std::max(1, SAMPLE_RATE * opt.chunk_ms / 1000);
        const int chunk_bytes = chunk_frames * CHANNELS * BYTES_PER_SAMPLE;

        pa_buffer_attr rec_attr{};
        rec_attr.maxlength = static_cast<uint32_t>(-1);
        rec_attr.tlength = static_cast<uint32_t>(-1);
        rec_attr.prebuf = static_cast<uint32_t>(-1);
        rec_attr.minreq = static_cast<uint32_t>(-1);
        rec_attr.fragsize = static_cast<uint32_t>(chunk_bytes);

        pa_buffer_attr play_attr{};
        play_attr.maxlength = static_cast<uint32_t>(bytes_per_second * 2);
        play_attr.tlength = static_cast<uint32_t>(bytes_per_second * opt.latency_ms / 1000);
        play_attr.prebuf = 0;
        play_attr.minreq = static_cast<uint32_t>(chunk_bytes);
        play_attr.fragsize = static_cast<uint32_t>(-1);

        int error = 0;

        pa_simple* rec = pa_simple_new(
            nullptr,
            "AudioCaptureRelay",
            PA_STREAM_RECORD,
            st.source_name.empty() ? nullptr : st.source_name.c_str(),
                                       "capture",
                                       &ss,
                                       nullptr,
                                       &rec_attr,
                                       &error
        );

        if (!rec) {
            std::cerr << "pa_simple_new(record) failed: " << pa_strerror(error) << "\n";
            g_running.store(false);
            return;
        }

        pa_simple* play = pa_simple_new(
            nullptr,
            "AudioCaptureRelay",
            PA_STREAM_PLAYBACK,
            nullptr,
            "relay playback",
            &ss,
            nullptr,
            &play_attr,
            &error
        );

        if (!play) {
            std::cerr << "pa_simple_new(playback) failed: " << pa_strerror(error) << "\n";
            pa_simple_free(rec);
            g_running.store(false);
            return;
        }

        std::vector<int16_t> input(chunk_frames * CHANNELS);
        std::vector<int16_t> output(chunk_frames * CHANNELS);

        while (g_running.load()) {
            if (pa_simple_read(rec, input.data(), input.size() * sizeof(int16_t), &error) < 0) {
                std::cerr << "pa_simple_read failed: " << pa_strerror(error) << "\n";
                st.errors.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            float vol = st.muted.load() ? 0.0f : st.volume.load();
            bool paused = st.paused.load();
            float analysis_vol = paused ? 0.0f : vol;

            update_analysis(st, input, chunk_frames, analysis_vol);

            if (paused) {
                std::fill(output.begin(), output.end(), 0);
            } else {
                for (size_t i = 0; i < input.size(); ++i) {
                    float v = static_cast<float>(input[i]) * vol;
                    v = std::clamp(v, -32768.0f, 32767.0f);
                    output[i] = static_cast<int16_t>(std::lround(v));
                }
            }

            if (pa_simple_write(play, output.data(), output.size() * sizeof(int16_t), &error) < 0) {
                std::cerr << "pa_simple_write failed: " << pa_strerror(error) << "\n";
                st.errors.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            st.frames_captured.fetch_add(chunk_frames);
        }

        pa_simple_drain(play, &error);
        pa_simple_free(play);
        pa_simple_free(rec);
    }

    void plain_status_loop(SharedState& st) {
        while (g_running.load()) {
            std::cout << "\r"
            << "vol=" << std::fixed << std::setprecision(0) << st.volume.load() * 100.0f << "%"
            << (st.muted.load() ? " muted" : "      ")
            << " peakL=" << std::setprecision(2) << st.peak_l.load()
            << " peakR=" << std::setprecision(2) << st.peak_r.load()
            << " frames=" << st.frames_captured.load()
            << " errors=" << st.errors.load()
            << "        "
            << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        std::cout << "\n";
    }

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto parsed = parse_args(argc, argv);
    if (!parsed) return 1;
    Options opt = *parsed;

    PulseSourceLister lister;
    std::string error;
    if (!lister.query(error)) {
        std::cerr << "Failed to query PulseAudio/PipeWire-Pulse sources: " << error << "\n";
        return 1;
    }

    const auto& sources = lister.sources();

    if (opt.list) {
        print_sources(sources);
        return 0;
    }

    auto selected = choose_source(sources, opt.source_arg, opt.interactive_select);
    if (!selected) {
        std::cerr << "\nUse --list to inspect available sources.\n";
        return 1;
    }

    SharedState st;
    st.source_name = selected->name;
    st.source_description = selected->description;
    st.source_is_monitor = selected->is_monitor;
    st.volume.store(opt.volume);
    st.waveform_enabled.store(opt.waveform);

    if (!opt.tui) {
        std::cout << "AudioCaptureRelay\n"
        << "  source: " << st.source_description << "\n"
        << "  name:   " << st.source_name << "\n"
        << "  type:   " << (st.source_is_monitor ? "MONITOR/output-capture" : "INPUT") << "\n"
        << "Press Ctrl+C to stop.\n";
    }

    std::thread audio(audio_thread, std::ref(st), std::cref(opt));

    if (opt.tui) draw_tui(st, opt);
    else plain_status_loop(st);

    g_running.store(false);
    if (audio.joinable()) audio.join();

    return 0;
}
