#include "domain/text_util.h"

#include <algorithm>
#include <cctype>

namespace acr {

    namespace {

        // UTF-8 の先頭バイトから、その文字のバイト長を返す。
        std::size_t utf8_step(unsigned char c) {
            if ((c & 0xE0) == 0xC0) return 2;
            if ((c & 0xF0) == 0xE0) return 3;
            if ((c & 0xF8) == 0xF0) return 4;
            return 1;
        }

        // limit バイトを超えない位置まで、文字境界で進んだバイト数を返す。
        std::size_t utf8_prefix_len(const std::string& s, int limit) {
            std::size_t cut = 0;
            while (cut < s.size() && static_cast<int>(cut) < limit) {
                std::size_t step = utf8_step(static_cast<unsigned char>(s[cut]));
                if (static_cast<int>(cut + step) > limit) break;
                cut += step;
            }
            return cut;
        }

    } // namespace

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
        if (static_cast<int>(s.size()) <= max_len) return s;
        if (max_len <= 3) return s.substr(0, utf8_prefix_len(s, max_len));
        return s.substr(0, utf8_prefix_len(s, max_len - 3)) + "...";
    }

    bool parse_index(const std::string& s, std::size_t& out) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try {
            out = static_cast<std::size_t>(std::stoull(s));
            return true;
        } catch (...) {
            return false;
        }
    }

} // namespace acr
