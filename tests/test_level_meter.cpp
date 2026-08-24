#include "domain/audio_format.h"
#include "domain/level_meter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acr;
using Catch::Approx;

namespace {

    std::vector<int16_t> stereo(std::size_t frames, int16_t l, int16_t r) {
        std::vector<int16_t> v(frames * CHANNELS);
        for (std::size_t f = 0; f < frames; ++f) {
            v[f * CHANNELS + 0] = l;
            v[f * CHANNELS + 1] = r;
        }
        return v;
    }

} // namespace

TEST_CASE("無音はすべて 0", "[meter]") {
    auto a = analyze_chunk(stereo(64, 0, 0), 64, 1.0f);
    CHECK(a.rms_l == 0.0f);
    CHECK(a.rms_r == 0.0f);
    CHECK(a.peak_l == 0.0f);
    CHECK(a.peak_r == 0.0f);
    CHECK(a.clip_ratio == 0.0f);
    CHECK(a.mono.size() == 64);
}

TEST_CASE("フルスケールはピーク 1.0 でクリップ扱い", "[meter]") {
    auto a = analyze_chunk(stereo(32, 32767, -32768), 32, 1.0f);
    CHECK(a.peak_l == Approx(1.0f).margin(0.001));
    CHECK(a.peak_r == Approx(1.0f).margin(0.001));
    CHECK(a.clip_ratio == Approx(1.0f));
}

TEST_CASE("volume が解析にも掛かる", "[meter]") {
    auto full = analyze_chunk(stereo(32, 16384, 16384), 32, 1.0f);
    auto half = analyze_chunk(stereo(32, 16384, 16384), 32, 0.5f);

    CHECK(half.peak_l == Approx(full.peak_l * 0.5f).margin(0.001));
    CHECK(half.rms_l == Approx(full.rms_l * 0.5f).margin(0.001));

    auto muted = analyze_chunk(stereo(32, 32767, 32767), 32, 0.0f);
    CHECK(muted.peak_l == 0.0f);
    CHECK(muted.clip_ratio == 0.0f); // ミュート中はクリップ警告を出さない
}

TEST_CASE("mono は左右の平均", "[meter]") {
    auto a = analyze_chunk(stereo(4, 16384, 0), 4, 1.0f);
    CHECK(a.mono.front() == Approx(0.25f).margin(0.001)); // (0.5 + 0.0) / 2
}

TEST_CASE("クリップ率は該当フレームの割合", "[meter]") {
    std::vector<int16_t> v = stereo(4, 0, 0);
    v[0] = 32767; // 1 フレーム目だけフルスケール
    auto a = analyze_chunk(v, 4, 1.0f);
    CHECK(a.clip_ratio == Approx(0.25f));
}

TEST_CASE("0 フレームでもゼロ除算しない", "[meter]") {
    auto a = analyze_chunk({}, 0, 1.0f);
    CHECK(a.rms_l == 0.0f);
    CHECK(a.clip_ratio == 0.0f);
    CHECK(a.mono.empty());
}
