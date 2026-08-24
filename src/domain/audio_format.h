#pragma once

#include <cstddef>

// PCM フォーマットと、フレーム <-> ミリ秒 の変換。
// このアプリ全体で S16LE / 48kHz / ステレオ 固定。
namespace acr {

    constexpr int SAMPLE_RATE = 48000;
    constexpr int CHANNELS = 2;
    constexpr int BYTES_PER_SAMPLE = 2;
    constexpr int DEFAULT_CHUNK_MS = 20;
    constexpr int DEFAULT_LATENCY_MS = 120;
    // 波形表示は生サンプルではなく、一定数ごとの min/max(エンベロープ)で持つ。
    // 生のまま 4 秒分(192,000 サンプル)を毎フレーム複製すると、表示するのは
    // せいぜい数百点なのに 768KB を毎秒 20 回コピーすることになる。加えて、
    // 列ごとに 1 点だけ拾う描き方はピークを取りこぼす(実際より小さく見える)。
    constexpr std::size_t WAVE_BUCKET_SAMPLES = 128;                 // 約 2.7ms
    constexpr std::size_t WAVE_HISTORY_BUCKETS = static_cast<std::size_t>(SAMPLE_RATE) * 4 / WAVE_BUCKET_SAMPLES; // 4 秒分

    constexpr int frames_per_ms_span(int ms) {
        return SAMPLE_RATE * ms / 1000;
    }

    constexpr double frames_to_ms(double frames) {
        return frames * 1000.0 / SAMPLE_RATE;
    }

} // namespace acr
