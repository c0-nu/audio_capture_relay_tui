#include "domain/text_util.h"

#include <catch2/catch_test_macros.hpp>

using namespace acr;

TEST_CASE("shorten は収まる文字列をそのまま返す", "[text]") {
    CHECK(shorten("abc", 10) == "abc");
    CHECK(shorten("abc", 3) == "abc");
    CHECK(shorten("", 5).empty());
}

TEST_CASE("shorten は max_len を超えない", "[text]") {
    CHECK(shorten("abcdefghij", 5) == "ab...");
    CHECK(shorten("abcdefghij", 5).size() == 5);
    CHECK(shorten("abcdefghij", 0).empty());
}

TEST_CASE("shorten は UTF-8 の途中で切らない", "[text]") {
    // "あいうえお" は 1 文字 3 バイト。
    const std::string s = "あいうえお";
    REQUIRE(s.size() == 15);

    // 本体 4 バイト分 + "..." に収まるのは "あ"(3 バイト)まで。
    const std::string cut = shorten(s, 7);
    CHECK(cut == "あ...");
    CHECK(cut.size() <= 7);

    // max_len <= 3 のときは "..." を付けず、文字境界で切るだけ。
    CHECK(shorten(s, 3) == "あ");
    CHECK(shorten(s, 2).empty()); // 1 文字も入らない
}

TEST_CASE("lower は ASCII を小文字にする", "[text]") {
    CHECK(lower("AbC_9") == "abc_9");
    CHECK(lower("日本語ABC") == "日本語abc");
}

TEST_CASE("parse_index は数字だけを受け付ける", "[text]") {
    std::size_t out = 999;
    CHECK(parse_index("0", out));
    CHECK(out == 0);
    CHECK(parse_index("42", out));
    CHECK(out == 42);

    CHECK_FALSE(parse_index("", out));
    CHECK_FALSE(parse_index("-1", out));
    CHECK_FALSE(parse_index(" 1", out));
    CHECK_FALSE(parse_index("1a", out));
    CHECK_FALSE(parse_index("99999999999999999999999999", out)); // stoull が投げる
}

TEST_CASE("safe は C 文字列と null を安全に値化する", "[text]") {
    CHECK(safe("pulse") == "pulse");
    CHECK(safe(nullptr).empty());
}

TEST_CASE("parse_int は整数全体だけを受け付ける", "[text]") {
    CHECK(parse_int("42") == 42);
    CHECK(parse_int("-7") == -7);
    CHECK_FALSE(parse_int("4ms").has_value());
    CHECK_FALSE(parse_int("").has_value());
    CHECK_FALSE(parse_int("99999999999999999999999999").has_value());
}

TEST_CASE("parse_float は有限の数値全体だけを受け付ける", "[text]") {
    REQUIRE(parse_float("12.5").has_value());
    CHECK(*parse_float("12.5") == 12.5f);
    CHECK(parse_float("-0.25") == -0.25f);
    CHECK_FALSE(parse_float("1.0x").has_value());
    CHECK_FALSE(parse_float("").has_value());
    CHECK_FALSE(parse_float("nan").has_value());
    CHECK_FALSE(parse_float("inf").has_value());
}
