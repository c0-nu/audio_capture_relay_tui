#include "domain/wave_history.h"
#include "domain/waveform.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace acr;

namespace {

    const std::string BLANK_CELL = "\xE2\xA0\x80"; // U+2800(点が 1 つも無い点字)

    bool has_dots(const std::string& line) {
        for (std::size_t i = 0; i + 3 <= line.size(); i += 3) {
            if (line.compare(i, 3, BLANK_CELL) != 0) return true;
        }
        return false;
    }

    std::vector<WaveBucket> flat(std::size_t n, float v = 0.0f) {
        return std::vector<WaveBucket>(n, WaveBucket{v, v, v});
    }

} // namespace

TEST_CASE("meter_bar", "[wave]") {
    CHECK(meter_bar(0.0f, 4) == "[----]");
    CHECK(meter_bar(1.0f, 4) == "[####]");
    CHECK(meter_bar(0.5f, 4) == "[##--]");
    CHECK(meter_bar(-5.0f, 4) == "[----]"); // 範囲外は丸める
    CHECK(meter_bar(5.0f, 4) == "[####]");
    CHECK(meter_bar(0.5f, 0) == "[]");
}

TEST_CASE("行数と 1 行の長さ", "[wave]") {
    auto lines = make_braille_waveform(flat(100), 10, 3);
    REQUIRE(lines.size() == 3);
    for (const auto& l : lines) CHECK(l.size() == 10 * 3); // 点字 1 文字 = 3 バイト
}

TEST_CASE("入力なし・サイズ 0", "[wave]") {
    auto empty_input = make_braille_waveform({}, 10, 3);
    REQUIRE(empty_input.size() == 3);
    for (const auto& l : empty_input) CHECK(l.empty());

    CHECK(make_braille_waveform(flat(10), 0, 3).empty());
    CHECK(make_braille_waveform(flat(10), 10, 0).empty());
}

TEST_CASE("無音は中央の 1 本になる", "[wave]") {
    auto lines = make_braille_waveform(flat(200, 0.0f), 20, 4);
    REQUIRE(lines.size() == 4);

    int rows_with_dots = 0;
    for (const auto& l : lines) rows_with_dots += has_dots(l) ? 1 : 0;
    CHECK(rows_with_dots == 1);

    CHECK_FALSE(has_dots(lines.front())); // 上端は空
    CHECK_FALSE(has_dots(lines.back()));  // 下端も空
}

TEST_CASE("列より多いバケットでもピークを取りこぼさない", "[wave]") {
    // 1 バケットだけフルスケール。列数が少なくて 1 列 = 数十バケットになっても、
    // その列は min/max を取るのでピークが残る(点サンプリングだと消えていた)。
    auto buckets = flat(1500, 0.0f);
    buckets[700] = WaveBucket{-1.0f, 1.0f, 0.0f};

    auto lines = make_braille_waveform(buckets, 10, 4);
    REQUIRE(lines.size() == 4);
    CHECK(has_dots(lines.front())); // 上端まで届く
    CHECK(has_dots(lines.back()));  // 下端まで届く
}

TEST_CASE("バケットが列より少なくても描ける", "[wave]") {
    auto lines = make_braille_waveform(flat(3, 0.5f), 40, 4);
    REQUIRE(lines.size() == 4);
    int rows_with_dots = 0;
    for (const auto& l : lines) rows_with_dots += has_dots(l) ? 1 : 0;
    CHECK(rows_with_dots >= 1);
}

TEST_CASE("WaveHistory は bucket_samples ごとに 1 バケット作る", "[wave]") {
    WaveHistory h(4, 100);

    h.append({1.0f, -1.0f, 0.0f, 0.0f}); // ちょうど 1 バケット
    auto s = h.snapshot();
    REQUIRE(s.size() == 1);
    CHECK(s[0].max == 1.0f);
    CHECK(s[0].min == -1.0f);
    CHECK(s[0].last == 0.0f); // 区間の最後の生サンプル(線表示が拾う)

    h.append({0.5f, 0.5f}); // 端数はまだバケットにならない
    CHECK(h.snapshot().size() == 1);

    h.append({0.5f, 0.5f}); // ここで 2 つ目が完成
    s = h.snapshot();
    REQUIRE(s.size() == 2);
    CHECK(s[1].max == 0.5f);
    CHECK(s[1].min == 0.5f);
}

TEST_CASE("WaveHistory は容量を超えたら古い方から捨てる", "[wave]") {
    WaveHistory h(2, 3);

    for (int i = 0; i < 10; ++i) {
        float v = static_cast<float>(i) / 10.0f;
        h.append({v, v});
    }

    auto s = h.snapshot();
    REQUIRE(s.size() == 3);
    CHECK(s.back().max == 0.9f); // 直近が残る
    CHECK(s.front().max == 0.7f);
}

TEST_CASE("スタイル名の相互変換", "[wave]") {
    CHECK(std::string(waveform_style_name(WaveformStyle::Envelope)) == "envelope");
    CHECK(std::string(waveform_style_name(WaveformStyle::Line)) == "line");

    CHECK(parse_waveform_style("envelope") == WaveformStyle::Envelope);
    CHECK(parse_waveform_style("line") == WaveformStyle::Line);
    CHECK_FALSE(parse_waveform_style("bogus").has_value());
    CHECK_FALSE(parse_waveform_style("").has_value());
    CHECK_FALSE(parse_waveform_style("Line").has_value()); // 大文字小文字は区別する
}

TEST_CASE("線表示は代表サンプルだけを拾う", "[wave]") {
    // min/max は ±1.0 まで振れているが、代表サンプル(last)は 0。
    // エンベロープは上下端まで塗り、線表示は中央の 1 本になる。
    std::vector<WaveBucket> buckets(300, WaveBucket{-1.0f, 1.0f, 0.0f});

    auto envelope = make_braille_waveform(buckets, 20, 4, WaveformStyle::Envelope);
    REQUIRE(envelope.size() == 4);
    CHECK(has_dots(envelope.front()));
    CHECK(has_dots(envelope.back()));

    auto line = make_braille_waveform(buckets, 20, 4, WaveformStyle::Line);
    REQUIRE(line.size() == 4);
    CHECK_FALSE(has_dots(line.front()));
    CHECK_FALSE(has_dots(line.back()));
}

TEST_CASE("線表示は隣の列と繋がる", "[wave]") {
    // 代表サンプルが ±1.0 を往復する。点が飛び飛びにならず、間が繋がること。
    std::vector<WaveBucket> buckets;
    for (int i = 0; i < 40; ++i) {
        float v = (i % 2) ? 1.0f : -1.0f;
        buckets.push_back(WaveBucket{v, v, v});
    }

    auto lines = make_braille_waveform(buckets, 20, 4, WaveformStyle::Line);
    REQUIRE(lines.size() == 4);
    for (const auto& l : lines) CHECK(has_dots(l));
}

TEST_CASE("既定はエンベロープ", "[wave]") {
    std::vector<WaveBucket> buckets(300, WaveBucket{-1.0f, 1.0f, 0.0f});
    CHECK(make_braille_waveform(buckets, 20, 4) == make_braille_waveform(buckets, 20, 4, WaveformStyle::Envelope));
}
