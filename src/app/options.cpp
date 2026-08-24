#include "app/options.h"

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
        << "  --select               Choose source interactively before starting.\n"
        << "  --volume PERCENT       Software output volume. Default: 100.\n"
        << "  --latency-ms MS         Playback buffer target. Default: 120.\n"
        << "  --chunk-ms MS           Read/write chunk size. Default: 20.\n"
        << "  --no-tui               Print plain status instead of full-screen TUI.\n"
        << "  --no-waveform          Start with waveform hidden.\n"
        << "  --waveform-style STYLE  envelope (default) or line. Switchable at runtime with s.\n"
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
                opt.relay.latency_ms = std::clamp(std::stoi(*v), 20, 2000);
            } else if (a == "--chunk-ms") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                opt.relay.chunk_ms = std::clamp(std::stoi(*v), 5, 200);
            } else if (a == "--no-tui") {
                opt.tui = false;
            } else if (a == "--no-waveform") {
                opt.waveform = false;
            } else if (a == "--waveform-style") {
                auto v = need_value(a);
                if (!v) return std::nullopt;
                auto style = parse_waveform_style(*v);
                if (!style) {
                    std::cerr << "Unknown waveform style: " << *v << " (expected envelope or line)\n";
                    return std::nullopt;
                }
                opt.waveform_style = *style;
            } else {
                std::cerr << "Unknown option: " << a << "\n";
                print_usage(argv[0]);
                return std::nullopt;
            }
        }

        if (!opt.tui) opt.waveform = false;
        return opt;
    }

} // namespace acr
