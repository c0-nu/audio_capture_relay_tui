#pragma once

#include "domain/wave_history.h"

#include <string>
#include <vector>

namespace acr {

    // buckets(時間順の min/max エンベロープ)を cols x rows の文字セルに、
    // Unicode 点字(U+2800〜)で描く。返すのは行ごとの UTF-8 文字列。端末には触らない。
    //
    // 1 列に複数バケットが対応する場合はその範囲の min/max を取るので、
    // 表示幅がいくつでもピークが消えない。
    std::vector<std::string> make_braille_waveform(const std::vector<WaveBucket>& buckets, int cols, int rows);

    // 0.0〜1.0 を "[###---]" 形式のバーにする。
    std::string meter_bar(float value, int width);

} // namespace acr
