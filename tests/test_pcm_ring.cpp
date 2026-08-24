#include "domain/audio_format.h"
#include "domain/pcm_ring.h"

#include <catch2/catch_test_macros.hpp>

using namespace acr;

namespace {

    // frames 個のフレームを作る。値はフレーム番号(+offset)。
    std::vector<int16_t> ramp(std::size_t frames, int offset = 0) {
        std::vector<int16_t> v(frames * CHANNELS);
        for (std::size_t f = 0; f < frames; ++f) {
            v[f * CHANNELS + 0] = static_cast<int16_t>(offset + static_cast<int>(f));
            v[f * CHANNELS + 1] = static_cast<int16_t>(offset + static_cast<int>(f));
        }
        return v;
    }

} // namespace

TEST_CASE("push した分だけ溜まり、pop した分だけ減る", "[ring]") {
    PcmRing ring;

    auto pushed = ring.push(ramp(100), 10000);
    CHECK_FALSE(pushed.trimmed);
    CHECK(ring.frames_buffered() == 100);

    auto popped = ring.pop(40);
    CHECK_FALSE(popped.underrun);
    CHECK(popped.samples.size() == 40 * CHANNELS);
    CHECK(ring.frames_buffered() == 60);
}

TEST_CASE("pop は先頭から順に返す", "[ring]") {
    PcmRing ring;
    ring.push(ramp(10), 10000);

    auto first = ring.pop(4);
    CHECK(first.samples.front() == 0);
    CHECK(first.samples.back() == 3);

    auto second = ring.pop(4);
    CHECK(second.samples.front() == 4);
}

TEST_CASE("足りなければ取れるだけ返して underrun を立てる", "[ring]") {
    PcmRing ring;
    ring.push(ramp(5), 10000);

    auto popped = ring.pop(20);
    CHECK(popped.underrun);
    CHECK(popped.samples.size() == 5 * CHANNELS);
    CHECK(ring.frames_buffered() == 0);

    auto empty = ring.pop(1);
    CHECK(empty.underrun);
    CHECK(empty.samples.empty());
}

TEST_CASE("空のリングから 0 フレーム要求しても underrun にしない", "[ring]") {
    PcmRing ring;
    auto popped = ring.pop(0);
    CHECK_FALSE(popped.underrun);
    CHECK(popped.samples.empty());
}

TEST_CASE("上限を超えたら古い方を落とす", "[ring]") {
    PcmRing ring;

    ring.push(ramp(100), 1000);
    auto pushed = ring.push(ramp(1000, 1000), 1000);

    CHECK(pushed.trimmed);
    CHECK(ring.frames_buffered() == 1000);

    // 残るのは新しい方。末尾はクロスフェードの影響を受けない。
    auto all = ring.pop(1000);
    CHECK(all.samples.back() == static_cast<int16_t>(1000 + 999));
}

TEST_CASE("trim は単独でも呼べて、落とした数を返す", "[ring]") {
    PcmRing ring;
    ring.push(ramp(500), 100000);

    CHECK(ring.trim(200) == 300);
    CHECK(ring.frames_buffered() == 200);

    CHECK(ring.trim(200) == 0);   // 既に収まっていれば何もしない
    CHECK(ring.trim(999) == 0);
    CHECK(ring.frames_buffered() == 200);
}
