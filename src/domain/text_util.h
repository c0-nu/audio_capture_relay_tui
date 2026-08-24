#pragma once

#include <cstddef>
#include <string>

// 文字列の小道具。UTF-8 を壊さずに切り詰める shorten が本体。
namespace acr {

    std::string safe(const char* s);
    std::string lower(std::string s);

    // max_len バイトに収まるよう UTF-8 の境界で切る。
    // 収まらない場合、max_len > 3 なら末尾に "..." を付ける。
    std::string shorten(const std::string& s, int max_len);

    // 全桁が数字のときだけ true。負号・空白は受け付けない。
    bool parse_index(const std::string& s, std::size_t& out);

} // namespace acr
