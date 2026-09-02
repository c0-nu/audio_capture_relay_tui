#include "domain/pcm_ring.h"

#include "domain/audio_format.h"
#include "domain/splice.h"

#include <algorithm>

namespace acr {

    void PcmRing::prepare(std::size_t max_frames, std::size_t max_push_frames) {
        std::lock_guard<std::mutex> lk(mutex_);
        ensure_capacity_locked((max_frames + max_push_frames) * CHANNELS);
    }

    void PcmRing::ensure_capacity_locked(std::size_t required_samples) {
        if (storage_.size() >= required_samples) return;

        std::vector<int16_t> grown(required_samples);
        for (std::size_t i = 0; i < size_samples_; ++i) {
            grown[i] = sample_at_locked(i);
        }
        storage_.swap(grown);
        head_ = 0;
    }

    int16_t& PcmRing::sample_at_locked(std::size_t logical_index) {
        return storage_[(head_ + logical_index) % storage_.size()];
    }

    PcmRing::PushResult PcmRing::push(const std::vector<int16_t>& samples, std::size_t max_frames) {
        std::lock_guard<std::mutex> lk(mutex_);

        // prepare 済みの通常経路ではここは no-op。テストや動的な利用でも
        // 従来どおり push 単体で使えるよう、不足時だけ拡張する。
        ensure_capacity_locked(std::max(max_frames * CHANNELS + samples.size(),
                                        size_samples_ + samples.size()));

        if (!samples.empty()) {
            const std::size_t tail = (head_ + size_samples_) % storage_.size();
            const std::size_t first = std::min(samples.size(), storage_.size() - tail);
            std::copy_n(samples.begin(), static_cast<std::ptrdiff_t>(first),
                        storage_.begin() + static_cast<std::ptrdiff_t>(tail));
            std::copy(samples.begin() + static_cast<std::ptrdiff_t>(first), samples.end(), storage_.begin());
            size_samples_ += samples.size();
        }

        PushResult result;
        result.trimmed = trim_locked(max_frames) > 0;

        frames_buffered_.store(size_samples_ / CHANNELS);
        return result;
    }

    std::size_t PcmRing::trim(std::size_t max_frames) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t dropped = trim_locked(max_frames);
        frames_buffered_.store(size_samples_ / CHANNELS);
        return dropped;
    }

    std::size_t PcmRing::trim_locked(std::size_t max_frames) {
        const std::size_t frames_now = size_samples_ / CHANNELS;
        if (frames_now <= max_frames) return 0;

        const std::size_t drop_frames = frames_now - max_frames;
        const std::size_t remaining_frames = frames_now - drop_frames;
        const std::size_t fade = std::min<std::size_t>({static_cast<std::size_t>(SPLICE_FADE_FRAMES),
            drop_frames,
                                                       remaining_frames});

        if (fade > 0) {
            // 落とす範囲の末尾を、残す範囲の先頭へ溶かし込む。
            // 循環バッファの物理末尾をまたぐ可能性があるので、論理添字で書く。
            const std::size_t drop_tail_start = (drop_frames - fade) * CHANNELS;
            const std::size_t keep_head_start = drop_frames * CHANNELS;
            for (std::size_t i = 0; i < fade; ++i) {
                float t = splice_gain(i, fade);
                for (int ch = 0; ch < CHANNELS; ++ch) {
                    std::size_t a_idx = drop_tail_start + i * CHANNELS + static_cast<std::size_t>(ch);
                    std::size_t b_idx = keep_head_start + i * CHANNELS + static_cast<std::size_t>(ch);
                    float a = static_cast<float>(sample_at_locked(a_idx));
                    float b = static_cast<float>(sample_at_locked(b_idx));
                    sample_at_locked(b_idx) = mix_sample(a, b, t);
                }
            }
        }

        const std::size_t drop_samples = drop_frames * CHANNELS;
        head_ = (head_ + drop_samples) % storage_.size();
        size_samples_ -= drop_samples;
        if (size_samples_ == 0) head_ = 0;
        return drop_frames;
    }

    PcmRing::PopResult PcmRing::pop(std::size_t frames, std::vector<int16_t>& out) {
        std::lock_guard<std::mutex> lk(mutex_);

        PopResult result;
        const std::size_t available_frames = size_samples_ / CHANNELS;
        const std::size_t take_frames = std::min(frames, available_frames);
        result.underrun = take_frames < frames;
        result.frames = take_frames;

        const std::size_t take_samples = take_frames * CHANNELS;
        out.resize(take_samples); // 確保済み容量があれば使い回される
        if (take_samples > 0) {
            const std::size_t first = std::min(take_samples, storage_.size() - head_);
            std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(head_),
                        static_cast<std::ptrdiff_t>(first), out.begin());
            std::copy_n(storage_.begin(), static_cast<std::ptrdiff_t>(take_samples - first),
                        out.begin() + static_cast<std::ptrdiff_t>(first));

            head_ = (head_ + take_samples) % storage_.size();
            size_samples_ -= take_samples;
            if (size_samples_ == 0) head_ = 0;
        }

        frames_buffered_.store(size_samples_ / CHANNELS);
        return result;
    }

} // namespace acr
