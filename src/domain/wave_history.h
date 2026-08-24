#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace acr {

    // 一定サンプル数ぶんの振れ幅。波形はこの単位で持つ。
    // last はその区間の最後の生サンプル。線表示(オシロ風)はこれを拾って繋ぐので、
    // エンベロープ表示と線表示を同じ履歴から描ける。
    struct WaveBucket {
        float min = 0.0f;
        float max = 0.0f;
        float last = 0.0f;
    };

    // 描画用に、モノラルミックスした直近の波形をエンベロープで溜めておく。
    // capture スレッドが書き、TUI スレッドが読む。
    class WaveHistory {
    public:
        WaveHistory(std::size_t bucket_samples, std::size_t capacity_buckets)
        : bucket_samples_(bucket_samples ? bucket_samples : 1), capacity_(capacity_buckets) {}

        // 端数はキャリーオーバーして次の append と合わせて 1 バケットにする。
        void append(const std::vector<float>& mono);

        std::vector<WaveBucket> snapshot() const;

    private:
        mutable std::mutex mutex_;
        std::deque<WaveBucket> buckets_;
        std::size_t bucket_samples_;
        std::size_t capacity_;

        WaveBucket partial_{};
        std::size_t partial_count_ = 0;
    };

} // namespace acr
