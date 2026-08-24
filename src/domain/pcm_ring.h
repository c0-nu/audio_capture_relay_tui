#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace acr {

    // capture スレッド(producer)と playback スレッド(consumer)が共有するリングバッファ。
    // インターリーブ済み S16LE ステレオ。
    //
    // ロックはこのクラスが持つ。呼び出し側が複数の操作をまとめてロックする必要が
    // ないよう、「積んで、溢れたら詰める」「取れるだけ取る」を 1 メソッドにしてある。
    class PcmRing {
    public:
        struct PushResult {
            bool trimmed = false;         // 安全弁(上限)が働いたか
        };

        struct PopResult {
            std::vector<int16_t> samples; // 実際に取れた分(要求より少ないことがある)
            bool underrun = false;        // 要求フレーム数に届かなかった
        };

        // samples を末尾に積む。max_frames を超えたら古い方を落とす。
        // 落とす際は、落とす範囲の末尾と残す範囲の先頭をクロスフェードして、
        // 新しい先頭にハードスプライスが残らないようにする。
        PushResult push(const std::vector<int16_t>& samples, std::size_t max_frames);

        // 先頭から最大 frames フレームを取り出す。
        PopResult pop(std::size_t frames);

        // max_frames を超えている分を古い方から落とす(push と同じくクロスフェードする)。
        // 起動時に貯まりすぎた分を捨てて目標水位から始めるために使う。
        // 落としたフレーム数を返す。
        std::size_t trim(std::size_t max_frames);

        // 現在のフレーム数。producer / consumer どちらの更新も**ロック内**で行うので、
        // 古い値が新しい値を上書きすることがない(ドリフト制御の入力なので順序が要る)。
        std::size_t frames_buffered() const { return frames_buffered_.load(); }

    private:
        // mutex_ を取った状態で呼ぶこと。落としたフレーム数を返す。
        std::size_t trim_locked(std::size_t max_frames);

        std::mutex mutex_;
        std::deque<int16_t> data_;
        std::atomic<std::size_t> frames_buffered_{0};
    };

} // namespace acr
