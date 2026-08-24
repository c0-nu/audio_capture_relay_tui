#include "domain/audio_format.h"
#include "domain/splice.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace acr;

TEST_CASE("splice_gain は 0 と 1 の間を単調に上がる", "[splice]") {
    const std::size_t fade = 8;
    float prev = 0.0f;
    for (std::size_t i = 0; i < fade; ++i) {
        float g = splice_gain(i, fade);
        CHECK(g > prev);
        CHECK(g > 0.0f);
        CHECK(g < 1.0f); // 端点で完全に入れ替わらない = 段差を作らない
        prev = g;
    }
}

TEST_CASE("crossfade_tail は dst 側から incoming 側へ移る", "[splice]") {
    const std::size_t fade = 8;
    std::vector<int16_t> dst(fade * CHANNELS, 1000);
    const std::vector<int16_t> incoming(fade * CHANNELS, 0);

    crossfade_tail(dst.data(), incoming.data(), fade, CHANNELS);

    // 先頭は元の値寄り、末尾は乗り移り先寄り。間は単調減少。
    CHECK(dst.front() < 1000);
    CHECK(dst.front() > dst.back());
    for (std::size_t f = 1; f < fade; ++f) {
        CHECK(dst[f * CHANNELS] <= dst[(f - 1) * CHANNELS]);
    }
}

TEST_CASE("同じ値どうしなら何も変わらない", "[splice]") {
    const std::size_t fade = 4;
    std::vector<int16_t> dst(fade * CHANNELS, 500);
    const std::vector<int16_t> incoming(fade * CHANNELS, 500);

    crossfade_tail(dst.data(), incoming.data(), fade, CHANNELS);

    for (int16_t v : dst) CHECK(v == 500);
}

TEST_CASE("fade 0 なら何もしない", "[splice]") {
    std::vector<int16_t> dst{123, 456};
    const std::vector<int16_t> incoming{0, 0};

    crossfade_tail(dst.data(), incoming.data(), 0, CHANNELS);

    CHECK(dst[0] == 123);
    CHECK(dst[1] == 456);
}
