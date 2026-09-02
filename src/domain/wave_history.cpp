#include "domain/wave_history.h"

#include <algorithm>

namespace acr {

    void WaveHistory::append(const std::vector<float>& mono) {
        std::lock_guard<std::mutex> lk(mutex_);

        for (float v : mono) {
            if (partial_count_ == 0) {
                partial_.min = v;
                partial_.max = v;
            } else {
                partial_.min = std::min(partial_.min, v);
                partial_.max = std::max(partial_.max, v);
            }
            partial_.last = v;

            if (++partial_count_ >= bucket_samples_) {
                partial_count_ = 0;
                if (buckets_.empty()) continue;

                const std::size_t tail = (head_ + size_) % buckets_.size();
                buckets_[tail] = partial_;
                if (size_ < buckets_.size()) {
                    ++size_;
                } else {
                    head_ = (head_ + 1) % buckets_.size();
                }
            }
        }
    }

    void WaveHistory::snapshot(std::vector<WaveBucket>& out) const {
        std::lock_guard<std::mutex> lk(mutex_);
        out.resize(size_);
        for (std::size_t i = 0; i < size_; ++i) {
            out[i] = buckets_[(head_ + i) % buckets_.size()];
        }
    }

    std::vector<WaveBucket> WaveHistory::snapshot() const {
        std::vector<WaveBucket> out;
        snapshot(out);
        return out;
    }

} // namespace acr
