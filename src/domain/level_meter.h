#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace acr {

    // 1 チャンク分の解析結果。
    struct ChunkAnalysis {
        float rms_l = 0.0f;
        float rms_r = 0.0f;
        float peak_l = 0.0f;
        float peak_r = 0.0f;
        float clip_ratio = 0.0f;   // クリップ寸前だったフレームの割合
        std::vector<float> mono;   // 波形表示用のモノラルミックス
    };

    // interleaved(S16LE ステレオ)を volume 倍して解析する。副作用なし。
    ChunkAnalysis analyze_chunk(const std::vector<int16_t>& interleaved, std::size_t frames, float volume);

} // namespace acr
