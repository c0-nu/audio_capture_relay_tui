#include "app/options.h"

#include "domain/text_util.h"

#include <algorithm>
#include <iostream>

namespace acr {

    void print_usage(const char* argv0) {
        std::cout
        << "Usage:\n"
        << "  " << argv0 << " --list\n"
        << "  " << argv0 << " [--source NAME_OR_INDEX] [--volume 80] [--latency-ms 120] [--chunk-ms 20]\n"
        << "\n"
        << "Options:\n"
        << "  --list                 List capture sources.\n"
        << "  --source VALUE          Source index, exact name, or substring. Omit to use default source.\n"
        << "  --sink VALUE            Output sink index, exact name, or substring. Omit to use default sink.\n"
        << "  --select               Choose source interactively before starting.\n"
        << "  --volume PERCENT       Software output volume. Default: 100.\n"
        << "  --latency-ms MS         Playback buffer target. Default: 120.\n"
        << "  --chunk-ms MS           Read/write chunk size. Default: 20.\n"
        << "  --no-tui               Print plain status instead of full-screen TUI.\n"
        << "  --no-waveform          Start with waveform hidden.\n"
        << "  --waveform-style STYLE  envelope (default) or line. Switchable at runtime with s.\n"
        << "  --low-latency          Shorthand for --chunk-ms 5 --latency-ms 60 (default is about 130ms).\n"
        << "                         Explicit --chunk-ms / --latency-ms still win.\n"
        << "  --version              Show version.\n"
        << "  --help                 Show this help.\n";
    }

    void print_version() {
#ifdef ACR_VERSION
        std::cout << "audio_capture_relay " << ACR_VERSION << "\n";
#else
        std::cout << "audio_capture_relay (unknown version)\n";
#endif
    }

    namespace {

        // 走らせずに終わる 2 通り。--help / --version は正常終了であってエラーではない。
        ParsedArgs done() { return ParsedArgs{std::nullopt, 0}; }
        ParsedArgs usage_error() { return ParsedArgs{std::nullopt, 2}; }

        ParsedArgs invalid_value(const std::string& name, const std::string& value) {
            std::cerr << "Invalid value for " << name << ": " << value << "\n";
            return usage_error();
        }

    } // namespace

    ParsedArgs parse_args(int argc, char** argv) {
        Options opt;

        // --low-latency はあくまで既定値の差し替え。明示された値のほうを常に優先する
        // (指定順に依存させない)。
        bool low_latency = false;
        bool chunk_given = false;
        bool latency_given = false;

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
                return done();
            } else if (a == "--version" || a == "-V") {
                print_version();
                return done();
            } else if (a == "--list") {
                opt.list = true;
            } else if (a == "--source" || a == "-s") {
                auto v = need_value(a);
                if (!v) return usage_error();
                opt.source_arg = *v;
            } else if (a == "--sink") {
                auto v = need_value(a);
                if (!v) return usage_error();
                opt.sink_arg = *v;
            } else if (a == "--select") {
                opt.interactive_select = true;
            } else if (a == "--volume" || a == "-v") {
                auto v = need_value(a);
                if (!v) return usage_error();
                auto percent = parse_float(*v);
                if (!percent) return invalid_value(a, *v);
                opt.volume = std::clamp(*percent / 100.0f, 0.0f, 4.0f);
            } else if (a == "--latency-ms") {
                auto v = need_value(a);
                if (!v) return usage_error();
                auto ms = parse_int(*v);
                if (!ms) return invalid_value(a, *v);
                opt.relay.latency_ms = std::clamp(*ms, 20, 2000);
                latency_given = true;
            } else if (a == "--chunk-ms") {
                auto v = need_value(a);
                if (!v) return usage_error();
                auto ms = parse_int(*v);
                if (!ms) return invalid_value(a, *v);
                opt.relay.chunk_ms = std::clamp(*ms, 5, 200);
                chunk_given = true;
            } else if (a == "--low-latency") {
                low_latency = true;
            } else if (a == "--no-tui") {
                opt.tui = false;
            } else if (a == "--no-waveform") {
                opt.waveform = false;
            } else if (a == "--waveform-style") {
                auto v = need_value(a);
                if (!v) return usage_error();
                auto style = parse_waveform_style(*v);
                if (!style) {
                    std::cerr << "Unknown waveform style: " << *v << " (expected envelope or line)\n";
                    return usage_error();
                }
                opt.waveform_style = *style;
            } else {
                std::cerr << "Unknown option: " << a << "\n";
                print_usage(argv[0]);
                return usage_error();
            }
        }

        if (low_latency) {
            if (!chunk_given) opt.relay.chunk_ms = LOW_LATENCY_CHUNK_MS;
            if (!latency_given) opt.relay.latency_ms = LOW_LATENCY_TARGET_MS;
        }

        if (!opt.tui) opt.waveform = false;
        return ParsedArgs{opt, 0};
    }

} // namespace acr
