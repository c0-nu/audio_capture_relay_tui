#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// 波形をつなぎ替える場所(バッファのトリム / 余分に消費したチャンクの捨て際)で
// ハードスプライスによるクリックを出さないためのクロスフェード。
namespace acr {

    // クロスフェード長の上限。1 フレーム単位の微修正が主なので、これで十分。
    constexpr int SPLICE_FADE_FRAMES = 8;

    // i 番目のフレームでの、乗り移り先の重み。0 < t < 1。
    inline float splice_gain(std::size_t i, std::size_t fade_frames) {
        return static_cast<float>(i + 1) / static_cast<float>(fade_frames + 1);
    }

    inline int16_t mix_sample(float from, float to, float t) {
        return static_cast<int16_t>(std::lround(from * (1.0f - t) + to * t));
    }

    // dst_tail(fade_frames フレーム分)を incoming へ向けて溶かし込む。
    // dst_tail / incoming はどちらもインターリーブ済みで channels チャンネル。
    void crossfade_tail(int16_t* dst_tail, const int16_t* incoming, std::size_t fade_frames, int channels);

} // namespace acr
