#include "domain/splice.h"

namespace acr {

    void crossfade_tail(int16_t* dst_tail, const int16_t* incoming, std::size_t fade_frames, int channels) {
        for (std::size_t i = 0; i < fade_frames; ++i) {
            float t = splice_gain(i, fade_frames);
            for (int ch = 0; ch < channels; ++ch) {
                std::size_t idx = i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch);
                float a = static_cast<float>(dst_tail[idx]);
                float b = static_cast<float>(incoming[idx]);
                dst_tail[idx] = mix_sample(a, b, t);
            }
        }
    }

} // namespace acr
