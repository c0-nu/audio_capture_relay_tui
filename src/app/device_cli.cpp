#include "app/device_cli.h"

#include "domain/device_match.h"
#include "domain/text_util.h"

#include <iostream>

namespace acr {

    void print_sources(const std::vector<DeviceInfo>& sources) {
        std::cout << "Capture sources:\n\n";
        for (std::size_t i = 0; i < sources.size(); ++i) {
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

    void print_sinks(const std::vector<DeviceInfo>& sinks) {
        std::cout << "Output sinks:\n\n";
        for (std::size_t i = 0; i < sinks.size(); ++i) {
            const auto& s = sinks[i];
            std::cout << "[" << i << "] "
            << (s.is_default ? "* " : "  ")
            << s.description << "\n"
            << "    name: " << s.name << "\n\n";
        }
    }

    std::optional<DeviceInfo> choose_sink(const std::vector<DeviceInfo>& sinks,
                                          const std::string& sink_arg,
                                          bool& failed) {
        failed = false;
        if (sinks.empty()) {
            std::cerr << "No output sinks available.\n";
            failed = true;
            return std::nullopt;
        }
        if (sink_arg.empty()) return std::nullopt; // 既定の sink に任せる

        DeviceMatch match = match_device(sinks, sink_arg);
        switch (match.status) {
            case DeviceMatch::Status::Ok:
                return match.source;
            case DeviceMatch::Status::NoMatch:
                std::cerr << "No sink matched: " << sink_arg << "\n";
                break;
            case DeviceMatch::Status::Ambiguous:
                std::cerr << "Sink argument is ambiguous:\n";
                for (const auto& s : match.candidates) {
                    std::cerr << "  - " << s.description << " [" << s.name << "]\n";
                }
                break;
            case DeviceMatch::Status::Empty:
                std::cerr << "No output sinks available.\n";
                break;
        }

        failed = true;
        return std::nullopt;
    }

    std::optional<DeviceInfo> choose_source(const std::vector<DeviceInfo>& sources,
                                            const std::string& source_arg,
                                            bool interactive) {
        if (sources.empty()) return std::nullopt;

        if (interactive) {
            print_sources(sources);
            std::cout << "Select source number: " << std::flush;
            std::string line;
            std::getline(std::cin, line);
            std::size_t idx = 0;
            if (parse_index(line, idx) && idx < sources.size()) {
                return sources[idx];
            }
            std::cerr << "Invalid source index.\n";
            return std::nullopt;
        }

        DeviceMatch match = match_device(sources, source_arg);
        switch (match.status) {
            case DeviceMatch::Status::Ok:
                return match.source;
            case DeviceMatch::Status::NoMatch:
                std::cerr << "No source matched: " << source_arg << "\n";
                return std::nullopt;
            case DeviceMatch::Status::Ambiguous:
                std::cerr << "Source argument is ambiguous:\n";
                for (const auto& s : match.candidates) {
                    std::cerr << "  - " << s.description << " [" << s.name << "]\n";
                }
                return std::nullopt;
            case DeviceMatch::Status::Empty:
                return std::nullopt;
        }
        return std::nullopt;
    }

} // namespace acr
