#include "domain/output_mix.h"

#include "domain/audio_format.h"
#include "domain/splice.h"

#include <algorithm>
#include <cmath>

namespace acr {

    namespace {

        // 枯れて n フレーム目に流す音の減衰。PAD_FADE_FRAMES で 0 になる。
        float starve_gain(int starved_frames) {
            if (starved_frames >= PAD_FADE_FRAMES) return 0.0f;
            return 1.0f - static_cast<float>(starved_frames) / static_cast<float>(PAD_FADE_FRAMES);
        }

    } // namespace

    FillResult assemble_output(std::vector<std::int16_t>& output,
                               const std::vector<std::int16_t>& popped,
                               int chunk_frames,
                               PaddingState& pad) {
        FillResult result;
        if (chunk_frames <= 0) return result;

        const std::size_t chunk_samples = static_cast<std::size_t>(chunk_frames) * CHANNELS;
        const std::size_t popped_frames = popped.size() / CHANNELS;
        const std::size_t real_frames = std::min(popped_frames, static_cast<std::size_t>(chunk_frames));

        if (real_frames > 0) {
            std::copy(popped.begin(), popped.begin() + static_cast<std::ptrdiff_t>(real_frames * CHANNELS), output.begin());
        }

        if (popped_frames > static_cast<std::size_t>(chunk_frames)) {
            // ドリフト補正で多めに消費した分。そのまま切ると段差になるので、
            // 捨てる側の頭を出力の末尾へ溶かし込む。フェード長は「余分に持って
            // いるフレーム数」を超えられない(超えると popped の外を読む)。
            const std::size_t extra_frames = popped_frames - static_cast<std::size_t>(chunk_frames);
            const std::size_t fade = std::min<std::size_t>({static_cast<std::size_t>(SPLICE_FADE_FRAMES),
                static_cast<std::size_t>(chunk_frames),
                                                           extra_frames});
            crossfade_tail(output.data() + (static_cast<std::size_t>(chunk_frames) - fade) * CHANNELS,
                           popped.data() + chunk_samples,
                           fade,
                           CHANNELS);
        }

        // 直前まで埋めていたなら、戻ってきた音を立ち上げる。
        //
        // ランプ長は「埋め終わりに実際どこまで落ちていたか」に比例させること。
        // 固定長にすると、ドリフト補正で 1 フレームだけ埋めた(= まったく減衰
        // していない)場合にも 1ms のランプが掛かり、**何ともない実音声に穴を
        // 空ける**。実測で --low-latency では毎秒 2 回ここを踏んでいて、それが
        // プチノイズの正体だった。枯れが 0 まで落ちきった場合は resume_gain が
        // 0 になり、従来どおり PAD_RECOVER_FRAMES 全部を使う。
        if (pad.starved_frames > 0 && real_frames > 0) {
            const float resume_gain = starve_gain(pad.starved_frames);
            const std::size_t ramp = std::min<std::size_t>(
                static_cast<std::size_t>(std::lround(PAD_RECOVER_FRAMES * (1.0f - resume_gain))),
                real_frames);
            for (std::size_t f = 0; f < ramp; ++f) {
                const float t = static_cast<float>(f + 1) / static_cast<float>(ramp + 1);
                const float gain = resume_gain + (1.0f - resume_gain) * t;
                for (int ch = 0; ch < CHANNELS; ++ch) {
                    std::size_t i = f * CHANNELS + static_cast<std::size_t>(ch);
                    output[i] = static_cast<std::int16_t>(std::lround(static_cast<float>(output[i]) * gain));
                }
            }
            result.recovered_frames = static_cast<int>(ramp);
        }

        if (real_frames > 0) {
            pad.last_l = output[(real_frames - 1) * CHANNELS + 0];
            pad.last_r = output[(real_frames - 1) * CHANNELS + 1];
            pad.starved_frames = 0;
        }

        // 足りない分を埋める。保持しっぱなしにせず 0 へ落としていく。
        for (std::size_t f = real_frames; f < static_cast<std::size_t>(chunk_frames); ++f) {
            const float gain = starve_gain(pad.starved_frames + static_cast<int>(f - real_frames));
            output[f * CHANNELS + 0] = static_cast<std::int16_t>(std::lround(static_cast<float>(pad.last_l) * gain));
            output[f * CHANNELS + 1] = static_cast<std::int16_t>(std::lround(static_cast<float>(pad.last_r) * gain));
        }

        result.padded_frames = static_cast<int>(static_cast<std::size_t>(chunk_frames) - real_frames);
        pad.starved_frames += result.padded_frames;
        return result;
    }

    void apply_volume(std::vector<std::int16_t>& output, bool paused, bool muted, float volume) {
        if (paused) {
            std::fill(output.begin(), output.end(), 0);
            return;
        }

        const float vol = muted ? 0.0f : volume;
        for (auto& s : output) {
            float v = static_cast<float>(s) * vol;
            v = std::clamp(v, -32768.0f, 32767.0f);
            s = static_cast<std::int16_t>(std::lround(v));
        }
    }

} // namespace acr
