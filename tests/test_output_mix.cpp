#include "domain/audio_format.h"
#include "domain/output_mix.h"

#include <catch2/catch_test_macros.hpp>

using namespace acr;

namespace {

    constexpr int CHUNK = 960;

    std::vector<std::int16_t> constant(std::size_t frames, std::int16_t v) {
        return std::vector<std::int16_t>(frames * CHANNELS, v);
    }

    std::vector<std::int16_t> zeroed_output() {
        return std::vector<std::int16_t>(static_cast<std::size_t>(CHUNK) * CHANNELS, 0);
    }

} // namespace

TEST_CASE("足りているときはそのまま流す", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(CHUNK, 1000), CHUNK, pad);

    for (auto s : output) CHECK(s == 1000);
    CHECK(pad.starved_frames == 0);
    CHECK(pad.last_l == 1000);
}

TEST_CASE("多めに取れた分は末尾をクロスフェードして捨てる", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    std::vector<std::int16_t> popped = constant(CHUNK, 1000);
    auto extra = constant(20, 0); // 捨てる側は 0
    popped.insert(popped.end(), extra.begin(), extra.end());

    assemble_output(output, popped, CHUNK, pad);

    CHECK(output.front() == 1000);           // 頭は素通し
    CHECK(output.back() < 1000);             // 末尾は捨てる側へ寄っている
    CHECK(output.back() > 0);
}

TEST_CASE("枯れたら保持せず 0 へ落とす", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    // まず本物を流して、保持する値を作る。
    assemble_output(output, constant(CHUNK, 8000), CHUNK, pad);
    REQUIRE(pad.last_l == 8000);

    // 以降ずっと枯れる。
    assemble_output(output, {}, CHUNK, pad);
    CHECK(output.front() == 8000);                   // 最初のフレームは保持値
    CHECK(output.back() < output.front());           // すぐ減衰し始める
    CHECK(pad.starved_frames == CHUNK);

    // PAD_FADE_FRAMES(240)は 1 チャンク(960)より短いので、この時点で 0。
    assemble_output(output, {}, CHUNK, pad);
    for (auto s : output) CHECK(s == 0);             // DC が残らない(回帰テスト)

    assemble_output(output, {}, CHUNK, pad);
    for (auto s : output) CHECK(s == 0);
}

TEST_CASE("減衰は単調", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;
    assemble_output(output, constant(CHUNK, 10000), CHUNK, pad);
    assemble_output(output, {}, CHUNK, pad);

    for (std::size_t f = 1; f < static_cast<std::size_t>(CHUNK); ++f) {
        CHECK(output[f * CHANNELS] <= output[(f - 1) * CHANNELS]);
    }
}

TEST_CASE("復帰時は立ち上げてから流す", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(CHUNK, 5000), CHUNK, pad);
    assemble_output(output, {}, CHUNK, pad);            // 枯れる
    REQUIRE(pad.starved_frames > 0);

    assemble_output(output, constant(CHUNK, 5000), CHUNK, pad); // 復帰

    CHECK(output.front() < 5000);                        // いきなり全開にしない
    CHECK(output.front() > 0);
    CHECK(output[static_cast<std::size_t>(PAD_RECOVER_FRAMES) * CHANNELS] == 5000); // ランプ後は素通し
    CHECK(pad.starved_frames == 0);
}

TEST_CASE("部分的に足りないときは実音声のあとを埋める", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(100, 3000), CHUNK, pad);

    CHECK(output[0] == 3000);
    CHECK(output[99 * CHANNELS] == 3000);
    CHECK(output[100 * CHANNELS] <= 3000);          // ここから埋め
    CHECK(output[100 * CHANNELS] > 0);
    CHECK(pad.starved_frames == CHUNK - 100);
}

TEST_CASE("chunk_frames が 0 なら何もしない", "[output]") {
    std::vector<std::int16_t> output;
    PaddingState pad;
    assemble_output(output, {}, 0, pad);
    CHECK(output.empty());
    CHECK(pad.starved_frames == 0);
}

TEST_CASE("apply_volume", "[output]") {
    auto output = constant(4, 10000);

    apply_volume(output, false, false, 0.5f);
    for (auto s : output) CHECK(s == 5000);

    apply_volume(output, false, true, 1.0f);   // mute
    for (auto s : output) CHECK(s == 0);

    output = constant(4, 10000);
    apply_volume(output, true, false, 1.0f);   // pause
    for (auto s : output) CHECK(s == 0);
}

TEST_CASE("apply_volume は範囲を超えない", "[output]") {
    auto output = constant(4, 30000);
    apply_volume(output, false, false, 4.0f);
    for (auto s : output) CHECK(s == 32767);
}

// --- 立ち上げランプの長さは「実際に減衰した分」に比例する ---
//
// ドリフト補正で 1 チャンク未満しか消費しないと、足りない分は埋め込みで埋まる。
// ここで固定長のランプを掛けると、枯れてもいない実音声に穴が空く(実測で
// --low-latency 時に毎秒 2 回踏んでいた。プチノイズの原因)。

TEST_CASE("1 フレームだけ埋めた次のチャンクは削らない", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(CHUNK, 10000), CHUNK, pad);

    // 補正で 1 フレームだけ少なく消費した(リングは枯れていない)。
    const auto fill = assemble_output(output, constant(CHUNK - 1, 10000), CHUNK, pad);
    CHECK(fill.padded_frames == 1);
    REQUIRE(pad.starved_frames == 1);

    // 次は全部実音声。1 フレーム分しか減衰していないのだから、ほぼ素通しのはず。
    const auto next = assemble_output(output, constant(CHUNK, 10000), CHUNK, pad);
    CHECK(next.recovered_frames == 0);
    CHECK(output.front() == 10000);
    for (auto s : output) CHECK(s == 10000);
}

TEST_CASE("少しだけ埋めたときのランプは短い", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(CHUNK, 10000), CHUNK, pad);
    assemble_output(output, constant(CHUNK - PAD_FADE_FRAMES / 2, 10000), CHUNK, pad);
    REQUIRE(pad.starved_frames == PAD_FADE_FRAMES / 2);

    // 半分まで落ちたのだから、ランプも半分程度で足りる。
    const auto next = assemble_output(output, constant(CHUNK, 10000), CHUNK, pad);
    CHECK(next.recovered_frames > 0);
    CHECK(next.recovered_frames < PAD_RECOVER_FRAMES);
    CHECK(output.front() > 10000 / 3);      // 落ちきる前から立ち上げる
}

TEST_CASE("落ちきってからの復帰は従来どおり全長のランプ", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    assemble_output(output, constant(CHUNK, 5000), CHUNK, pad);
    assemble_output(output, {}, CHUNK, pad);            // 1 チャンクまるごと枯れる
    REQUIRE(pad.starved_frames >= PAD_FADE_FRAMES);     // 0 まで落ちきっている

    const auto next = assemble_output(output, constant(CHUNK, 5000), CHUNK, pad);
    CHECK(next.recovered_frames == PAD_RECOVER_FRAMES);
    CHECK(output.front() < 5000);
    CHECK(output[static_cast<std::size_t>(PAD_RECOVER_FRAMES) * CHANNELS] == 5000);
}

TEST_CASE("埋めた分は FillResult に出る", "[output]") {
    auto output = zeroed_output();
    PaddingState pad;

    CHECK(assemble_output(output, constant(CHUNK, 100), CHUNK, pad).padded_frames == 0);
    CHECK(assemble_output(output, constant(100, 100), CHUNK, pad).padded_frames == CHUNK - 100);
    CHECK(assemble_output(output, {}, CHUNK, pad).padded_frames == CHUNK);
}
