#include "domain/drift_control.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace acr;

namespace {

    // 既定値相当: 48kHz / 20ms チャンク / 目標 120ms / 予備 40ms
    constexpr int CHUNK = 960;
    constexpr int TARGET = 5760;
    constexpr int MIN_RING = 1920;

    DriftController make(std::int64_t initial_ring = TARGET - 1920, std::int64_t initial_out = 1920) {
        return DriftController(TARGET, CHUNK, 20, MIN_RING, initial_ring, initial_out);
    }

} // namespace

TEST_CASE("目標どおりなら補正しない", "[drift]") {
    auto drift = make();
    for (int i = 0; i < 500; ++i) {
        auto d = drift.update(TARGET - 1920, 1920);
        REQUIRE(d.consume_frames == CHUNK);
        REQUIRE(d.drift_ms == 0);
        REQUIRE_FALSE(d.raised_target);
    }
}

TEST_CASE("チャンク単位のノコギリには反応しない", "[drift]") {
    // capture はまとめて届き playback は連続で減らすので、瞬間値は 1 チャンク幅で
    // 上下する。平均は目標どおりなのだから、ここに毎チャンク反応してはいけない
    // (それをやると周期的な「チッ」というノイズになる。平滑化の回帰テスト)。
    auto drift = make();

    int nudges = 0;
    int net_correction = 0;

    for (int i = 0; i < 500; ++i) { // 10 秒相当
        std::int64_t ring = TARGET - 1920 + ((i % 2) ? CHUNK / 2 : -CHUNK / 2);
        auto d = drift.update(ring, 1920);
        if (d.consume_frames != CHUNK) ++nudges;
        net_correction += d.consume_frames - CHUNK;
    }

    // 平滑化が無ければ毎チャンク動く(500 回)。数回に収まっていること。
    CHECK(nudges <= 3);
    // 10 秒でならして数フレーム(= 0.1ms 未満)しか動かない。
    CHECK(std::abs(net_correction) <= 3);
}

TEST_CASE("恒常的に溜まっていれば多めに消費する", "[drift]") {
    auto drift = make();
    int corrected = 0;
    int max_seen = 0;

    for (int i = 0; i < 400; ++i) { // 8 秒相当
        auto d = drift.update(TARGET, 4800); // 合計は目標 +100ms
        if (d.consume_frames > CHUNK) {
            ++corrected;
            max_seen = std::max(max_seen, d.consume_frames);
        }
    }

    CHECK(corrected > 0);
    CHECK(max_seen <= CHUNK + 48); // 1 チャンクあたりの上限(約 5%)を超えない
}

TEST_CASE("恒常的に枯れていれば少なめに消費する", "[drift]") {
    auto drift = make(TARGET / 2, 0);
    bool held_back = false;

    for (int i = 0; i < 400; ++i) {
        auto d = drift.update(TARGET / 2, 0); // 合計が目標の半分
        if (d.consume_frames < CHUNK) held_back = true;
        REQUIRE(d.consume_frames >= 0);
    }

    CHECK(held_back);
}

TEST_CASE("リングだけでなくサーバ側の滞留も見る", "[drift]") {
    // リングの水位は目標ぴったりでも、サーバ側に積まれていれば実レイテンシは超過。
    // リングだけを見ていた頃はここで「ちょうど良い」と誤判断していた。
    auto ring_only_ok = make();
    bool drains = false;

    for (int i = 0; i < 400; ++i) {
        auto d = ring_only_ok.update(TARGET, TARGET); // 合計は目標の 2 倍
        if (d.consume_frames > CHUNK) drains = true;
    }

    CHECK(drains);
}

TEST_CASE("届かない目標では実現できる一番浅い水位を狙う", "[drift]") {
    // サーバ側が目標(120ms)より深い環境 —— 例えば 200ms 抱えている。
    // いくら排出しても届かないので、狙いを「サーバ側 + リングの予備」に切り替える。
    // ここを開ループにすると(以前の実装)、リングの水位が成り行きになって
    // いずれ枯れる。回帰テスト。
    const std::int64_t deep_out = 9600; // 200ms
    auto drift = DriftController(TARGET, CHUNK, 20, MIN_RING, MIN_RING, deep_out);

    DriftController::Decision d{};
    for (int i = 0; i < 400; ++i) {
        d = drift.update(MIN_RING, deep_out);
    }

    CHECK(d.raised_target);
    CHECK(d.effective_target_frames == deep_out + MIN_RING);
    CHECK(d.consume_frames == CHUNK);  // 狙いどおりなので補正しない = 開ループにならない
    CHECK(d.drift_ms == 0);
}

TEST_CASE("届かない目標でも、薄くなれば貯めに行く", "[drift]") {
    const std::int64_t deep_out = 9600;
    auto drift = DriftController(TARGET, CHUNK, 20, MIN_RING, MIN_RING, deep_out);

    bool refills = false;
    for (int i = 0; i < 400; ++i) {
        auto d = drift.update(MIN_RING / 4, deep_out); // 予備を大きく割っている
        if (d.consume_frames < CHUNK) refills = true;
    }

    CHECK(refills);
}

TEST_CASE("届く目標なら押し上げない", "[drift]") {
    auto drift = make();
    auto d = drift.update(TARGET - 1920, 1920);
    CHECK_FALSE(d.raised_target);
    CHECK(d.effective_target_frames == TARGET);
}

TEST_CASE("drift_ms は目標との差を ms で返す", "[drift]") {
    auto drift = DriftController(TARGET, CHUNK, 20, MIN_RING, TARGET + 4800, 0);
    auto d = drift.update(TARGET + 4800, 0); // +100ms
    CHECK(d.drift_ms > 90);
    CHECK(d.drift_ms <= 100);
}
