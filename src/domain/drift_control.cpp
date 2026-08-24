#include "domain/drift_control.h"

#include "domain/audio_format.h"

#include <algorithm>
#include <cmath>

namespace acr {

    DriftController::DriftController(int target_total_frames,
                                     int chunk_frames,
                                     int chunk_ms,
                                     int min_ring_frames,
                                     std::int64_t initial_ring_frames,
                                     std::int64_t initial_downstream_frames)
    : target_total_frames_(target_total_frames),
    chunk_frames_(chunk_frames),
    min_ring_frames_(min_ring_frames),
    chunks_for_full_correction_(DRIFT_CORRECTION_SECONDS * (1000.0 / std::max(1, chunk_ms))),
    smoothed_total_(static_cast<double>(initial_ring_frames + initial_downstream_frames)),
    smoothed_downstream_(static_cast<double>(initial_downstream_frames)) {}

    DriftController::Decision DriftController::update(std::int64_t ring_frames, std::int64_t downstream_frames) {
        const std::int64_t total = ring_frames + downstream_frames;

        smoothed_total_ += (static_cast<double>(total) - smoothed_total_) * DRIFT_SMOOTHING_ALPHA;
        smoothed_downstream_ += (static_cast<double>(downstream_frames) - smoothed_downstream_) * DRIFT_SMOOTHING_ALPHA;

        // サーバ側が抱えている分は削れない。要求値がそれを下回るなら、
        // 実現できる一番浅いところ(サーバ側 + リングの予備)を狙う。
        const double floor_total = smoothed_downstream_ + static_cast<double>(min_ring_frames_);
        const double effective_target = std::max(static_cast<double>(target_total_frames_), floor_total);

        double smoothed_error = smoothed_total_ - effective_target;

        // Accumulate how much correction the current smoothed error calls
        // for, spread out over DRIFT_CORRECTION_SECONDS. Apply however many
        // whole frames of debt have built up (not just one) -- capping this
        // at exactly 1 frame per chunk regardless of how large the debt was
        // is a bug: real drift between two independent clocks can exceed
        // what a fixed 1-frame-per-chunk correction rate can keep up with,
        // in which case the buffer just drifts away unboundedly no matter
        // how long you wait. The generous ceiling below still keeps any
        // single chunk's correction small enough to be inaudible; it just
        // isn't hard-capped to a value that might be smaller than the real
        // drift rate.
        correction_debt_ += smoothed_error / chunks_for_full_correction_;

        const int max_correction_frames = std::max(8, chunk_frames_ / 20); // ~5% of a chunk
        int correction = static_cast<int>(std::trunc(correction_debt_));
        correction = std::clamp(correction, -max_correction_frames, max_correction_frames);
        correction_debt_ -= correction;

        Decision d;
        d.raised_target = effective_target > static_cast<double>(target_total_frames_) + 0.5;

        // 排出(多めに消費)はリングからしか引けない。予備を割ってまで引くと、
        // その場で枯れて sample-and-hold のパディングが入る。上の下限で普段は
        // ここまで来ないが、揺れで一時的に薄くなったときの保険。
        // 未払い分は上で清算済みなので、見送った補正は溜め直さない。
        if (correction > 0 && ring_frames - (chunk_frames_ + correction) < min_ring_frames_) {
            correction = 0;
        }

        d.consume_frames = std::max(0, chunk_frames_ + correction);
        d.smoothed_total_frames = static_cast<std::int64_t>(std::lround(smoothed_total_));
        d.effective_target_frames = static_cast<std::int64_t>(std::lround(effective_target));
        d.drift_ms = static_cast<std::int64_t>(std::lround(frames_to_ms(smoothed_error)));
        return d;
    }

} // namespace acr
