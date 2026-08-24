#include "domain/level_meter.h"

#include <algorithm>
#include <cmath>

namespace acr {

    ChunkAnalysis analyze_chunk(const std::vector<int16_t>& interleaved, std::size_t frames, float volume) {
        ChunkAnalysis out;
        out.mono.reserve(frames);

        double sum_l = 0.0;
        double sum_r = 0.0;
        float peak_l = 0.0f;
        float peak_r = 0.0f;
        std::uint64_t clips = 0;

        for (std::size_t f = 0; f < frames; ++f) {
            float l = (interleaved[f * 2 + 0] / 32768.0f) * volume;
            float r = (interleaved[f * 2 + 1] / 32768.0f) * volume;

            float al = std::abs(l);
            float ar = std::abs(r);

            if (al >= 0.999f || ar >= 0.999f) clips++;

            peak_l = std::max(peak_l, std::min(al, 1.0f));
            peak_r = std::max(peak_r, std::min(ar, 1.0f));
            sum_l += l * l;
            sum_r += r * r;

            out.mono.push_back(std::clamp((l + r) * 0.5f, -1.0f, 1.0f));
        }

        out.rms_l = static_cast<float>(std::sqrt(sum_l / std::max<std::size_t>(1, frames)));
        out.rms_r = static_cast<float>(std::sqrt(sum_r / std::max<std::size_t>(1, frames)));
        out.peak_l = peak_l;
        out.peak_r = peak_r;
        out.clip_ratio = static_cast<float>(clips) / std::max<std::size_t>(1, frames);
        return out;
    }

} // namespace acr
