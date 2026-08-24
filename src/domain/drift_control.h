#pragma once

#include <cstdint>

// --- Capture/playback drift correction ---
// Capture and playback run as two independent PulseAudio streams with their
// own clocks, so the amount of audio captured per second and the amount
// consumed per second are never exactly equal. Left uncorrected this drift
// accumulates until the playback buffer underruns (crackling/silence) or
// overruns (growing latency, eventually glitches). Instead of writing each
// captured chunk straight to playback, captured audio goes into a shared
// ring buffer, and the playback side gently speeds up or slows down its
// consumption to keep the buffered amount near the target latency.
//
// Real clock drift between two independent audio clocks is typically tens
// of parts-per-million -- on the order of a few samples per *second*, not
// per chunk. The naive approach of reacting to the buffer level every
// single chunk mistakes the natural "sawtooth" (capture arrives in bursts,
// playback drains continuously, so the instantaneous level swings by about
// one chunk's worth even with zero real drift) for drift, and ends up
// nudging on every chunk -- which is exactly what produces a periodic,
// audible tick. So instead:
//   1. The buffer level is smoothed (exponential moving average) to filter
//      out the chunk-to-chunk sawtooth and see only the slow, real trend.
//   2. Any correction needed is accumulated as a fractional "debt" and only
//      actually applied -- one single frame at a time -- once that debt
//      crosses a whole frame. In normal operation this fires at most a
//      couple of times per second, not every chunk.
//
// 目標が実現可能とは限らない。サーバ側キューとデバイスのバッファが目標より深い
// 環境では(例: --latency-ms 20 でサーバが 70ms 抱えている)、いくら排出しても
// 届かない。そこで実際に狙う水位を
//     max(要求値, サーバ側の滞留 + リングの予備)
// とする。こうしないと「常に排出したいが予備を割れないので何もしない」という
// 開ループ状態になり、リングの水位が成り行き任せになる(いずれ枯れる)。
//
// 制御対象は「自分のリングバッファ + サーバ側キュー」の**合計**。
// リングだけを見ていると、起動直後にサーバ側キューへ音が移った分を「減った」と
// 誤認し、目標へ戻すのに十数秒かける(その間バッファが浅く、枯れやすい)。
// サーバ側の滞留量は pa_simple_get_latency() で毎チャンク取れるので、それを足した
// 合計で判断する。こうすると起動直後の落ち込みが無くなり、表示するレイテンシも
// 実際に耳へ届くまでの時間と一致する。
namespace acr {

    constexpr double DRIFT_SMOOTHING_ALPHA = 1.0 / 64.0; // ~1.3s time constant at 20ms chunks
    constexpr double DRIFT_CORRECTION_SECONDS = 4.0;     // time to fully absorb the current smoothed error

    // 合計レイテンシから「今回いくつフレームを消費するか」を決める。
    // 状態は平滑値と補正の未払い分だけ。I/O には触れない。
    class DriftController {
    public:
        struct Decision {
            int consume_frames = 0;                 // このチャンクで取り出すフレーム数
            std::int64_t smoothed_total_frames = 0; // 平滑後の合計滞留(表示用)
            std::int64_t effective_target_frames = 0; // 実際に狙っている水位(下限で押し上げた後)
            std::int64_t drift_ms = 0;              // 目標との差。+ は溜まりすぎ、- は枯れ気味
            bool raised_target = false;             // 要求値では届かないので押し上げた
        };

        // target_total_frames … リング + サーバ側キューの目標
        // min_ring_frames   … リングに常に残しておく予備(サーバ側が目標より深い環境で、
        //                     排出し続けて枯らさないための下限)
        DriftController(int target_total_frames,
                        int chunk_frames,
                        int chunk_ms,
                        int min_ring_frames,
                        std::int64_t initial_ring_frames,
                        std::int64_t initial_downstream_frames);

        Decision update(std::int64_t ring_frames, std::int64_t downstream_frames);

    private:
        int target_total_frames_;
        int chunk_frames_;
        int min_ring_frames_;
        double chunks_for_full_correction_;
        double smoothed_total_;
        double smoothed_downstream_;
        double correction_debt_ = 0.0;
    };

} // namespace acr
