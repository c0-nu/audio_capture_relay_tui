#pragma once

#include "domain/wave_history.h"

#include <optional>
#include <string>
#include <vector>

namespace acr {

    enum class WaveformStyle {
        Envelope, // 列ごとの min〜max を塗る。表示幅が狭くてもピークが消えない
        Line,     // 列ごとに代表サンプルを 1 点拾って繋ぐ。オシロ風の細い線
    };

    // TUI が描画間で使い回す作業領域と出力。ホットパスなので、
    // 不変値を返す通常のドメイン API の例外として in-place 書き込みにする。
    struct WaveformRenderBuffer {
        std::vector<unsigned char> cells;
        std::vector<std::string> lines;
    };

    // out の容量を再利用して描画する。結果は out.lines。
    void render_braille_waveform(const std::vector<WaveBucket>& buckets,
                                 int cols,
                                 int rows,
                                 WaveformStyle style,
                                 WaveformRenderBuffer& out);

    // buckets(時間順のエンベロープ)を cols x rows の文字セルに、
    // Unicode 点字(U+2800〜)で描く値返しの便利 API。ホットパスでは上を使う。
    std::vector<std::string> make_braille_waveform(const std::vector<WaveBucket>& buckets,
                                                   int cols,
                                                   int rows,
                                                   WaveformStyle style = WaveformStyle::Envelope);

    // スタイル名(表示・CLI 用)。
    const char* waveform_style_name(WaveformStyle style);

    // "envelope" / "line" を解釈する。未知なら nullopt。
    std::optional<WaveformStyle> parse_waveform_style(const std::string& s);

    // 0.0〜1.0 を "[###---]" 形式のバーにする。
    std::string meter_bar(float value, int width);

} // namespace acr
