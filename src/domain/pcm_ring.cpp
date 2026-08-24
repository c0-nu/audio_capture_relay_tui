#include "domain/pcm_ring.h"

#include "domain/audio_format.h"
#include "domain/splice.h"

#include <algorithm>

namespace acr {

    PcmRing::PushResult PcmRing::push(const std::vector<int16_t>& samples, std::size_t max_frames) {
        std::lock_guard<std::mutex> lk(mutex_);

        data_.insert(data_.end(), samples.begin(), samples.end());

        PushResult result;
        result.trimmed = trim_locked(max_frames) > 0;

        frames_buffered_.store(data_.size() / CHANNELS);
        return result;
    }

    std::size_t PcmRing::trim(std::size_t max_frames) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t dropped = trim_locked(max_frames);
        frames_buffered_.store(data_.size() / CHANNELS);
        return dropped;
    }

    std::size_t PcmRing::trim_locked(std::size_t max_frames) {
        const std::size_t frames_now = data_.size() / CHANNELS;
        if (frames_now <= max_frames) return 0;

        const std::size_t drop_frames = frames_now - max_frames;
        const std::size_t remaining_frames = frames_now - drop_frames;
        const std::size_t fade = std::min<std::size_t>({static_cast<std::size_t>(SPLICE_FADE_FRAMES),
            drop_frames,
                                                       remaining_frames});

        if (fade > 0) {
            // 落とす範囲の末尾を、残す範囲の先頭へ溶かし込む。
            // deque なので連続メモリを仮定できず、ここだけは添字で書く。
            const std::size_t drop_tail_start = (drop_frames - fade) * CHANNELS;
            const std::size_t keep_head_start = drop_frames * CHANNELS;
            for (std::size_t i = 0; i < fade; ++i) {
                float t = splice_gain(i, fade);
                for (int ch = 0; ch < CHANNELS; ++ch) {
                    std::size_t a_idx = drop_tail_start + i * CHANNELS + static_cast<std::size_t>(ch);
                    std::size_t b_idx = keep_head_start + i * CHANNELS + static_cast<std::size_t>(ch);
                    float a = static_cast<float>(data_[a_idx]);
                    float b = static_cast<float>(data_[b_idx]);
                    data_[b_idx] = mix_sample(a, b, t);
                }
            }
        }

        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(drop_frames * CHANNELS));
        return drop_frames;
    }

    PcmRing::PopResult PcmRing::pop(std::size_t frames) {
        std::lock_guard<std::mutex> lk(mutex_);

        PopResult result;
        std::size_t available_frames = data_.size() / CHANNELS;
        std::size_t take_frames = std::min(frames, available_frames);
        result.underrun = take_frames < frames;

        std::size_t take_samples = take_frames * CHANNELS;
        result.samples.assign(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(take_samples));
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(take_samples));

        frames_buffered_.store(data_.size() / CHANNELS);
        return result;
    }

} // namespace acr
