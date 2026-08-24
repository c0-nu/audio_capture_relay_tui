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

            if (++partial_count_ >= bucket_samples_) {
                buckets_.push_back(partial_);
                partial_count_ = 0;
                if (buckets_.size() > capacity_) buckets_.pop_front();
            }
        }
    }

    std::vector<WaveBucket> WaveHistory::snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return std::vector<WaveBucket>(buckets_.begin(), buckets_.end());
    }

} // namespace acr
