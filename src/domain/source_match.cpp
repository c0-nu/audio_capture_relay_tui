#include "domain/source_match.h"

#include "domain/text_util.h"

#include <algorithm>

namespace acr {

    void sort_sources(std::vector<SourceInfo>& sources) {
        std::sort(sources.begin(), sources.end(), [](const SourceInfo& a, const SourceInfo& b) {
            if (a.is_default != b.is_default) return a.is_default > b.is_default;
            if (a.is_monitor != b.is_monitor) return a.is_monitor < b.is_monitor;
            return a.description < b.description;
        });
    }

    SourceMatch match_source(const std::vector<SourceInfo>& sources, const std::string& source_arg) {
        SourceMatch result;
        if (sources.empty()) {
            result.status = SourceMatch::Status::Empty;
            return result;
        }

        if (source_arg.empty()) {
            auto it = std::find_if(sources.begin(), sources.end(), [](const SourceInfo& s) {
                return s.is_default;
            });
            result.status = SourceMatch::Status::Ok;
            result.source = (it != sources.end()) ? *it : sources.front();
            return result;
        }

        std::size_t idx = 0;
        if (parse_index(source_arg, idx) && idx < sources.size()) {
            result.status = SourceMatch::Status::Ok;
            result.source = sources[idx];
            return result;
        }

        auto exact = std::find_if(sources.begin(), sources.end(), [&](const SourceInfo& s) {
            return s.name == source_arg || s.description == source_arg;
        });
        if (exact != sources.end()) {
            result.status = SourceMatch::Status::Ok;
            result.source = *exact;
            return result;
        }

        std::string q = lower(source_arg);
        std::vector<SourceInfo> matches;
        for (const auto& s : sources) {
            std::string hay = lower(s.name + "\n" + s.description);
            if (hay.find(q) != std::string::npos) matches.push_back(s);
        }

        if (matches.size() == 1) {
            result.status = SourceMatch::Status::Ok;
            result.source = matches.front();
            return result;
        }

        if (matches.empty()) {
            result.status = SourceMatch::Status::NoMatch;
        } else {
            result.status = SourceMatch::Status::Ambiguous;
            result.candidates = std::move(matches);
        }
        return result;
    }

} // namespace acr
