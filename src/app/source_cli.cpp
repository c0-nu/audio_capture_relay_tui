#include "app/source_cli.h"

#include "domain/source_match.h"
#include "domain/text_util.h"

#include <iostream>

namespace acr {

    void print_sources(const std::vector<SourceInfo>& sources) {
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

    std::optional<SourceInfo> choose_source(const std::vector<SourceInfo>& sources,
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

        SourceMatch match = match_source(sources, source_arg);
        switch (match.status) {
            case SourceMatch::Status::Ok:
                return match.source;
            case SourceMatch::Status::NoMatch:
                std::cerr << "No source matched: " << source_arg << "\n";
                return std::nullopt;
            case SourceMatch::Status::Ambiguous:
                std::cerr << "Source argument is ambiguous:\n";
                for (const auto& s : match.candidates) {
                    std::cerr << "  - " << s.description << " [" << s.name << "]\n";
                }
                return std::nullopt;
            case SourceMatch::Status::Empty:
                return std::nullopt;
        }
        return std::nullopt;
    }

} // namespace acr
