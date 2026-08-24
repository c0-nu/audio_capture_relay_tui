#pragma once

#include "domain/audio_format.h"

namespace acr {

    // --low-latency の中身。チャンクを詰めると、こちらが持つ予備
    // (min_ring = 2 チャンク)とサーバ側に持たせる分(4 チャンク)が両方縮む。
    // 実測(PipeWire, quantum 1024): 既定 約130ms -> この設定で 60ms(90 秒で underrun 0)。
    // 実測の下限は約 42ms だが、目標をそこぎりぎり(40ms)に置くと薄くて滑ったので
    // (60 秒で 1 回)、少し余裕を取ってある。
    constexpr int LOW_LATENCY_CHUNK_MS = 5;
    constexpr int LOW_LATENCY_TARGET_MS = 60;

    // 中継の動作パラメータ。CLI の解析結果から作られ、以降は読み取り専用。
    struct RelayConfig {
        int latency_ms = DEFAULT_LATENCY_MS;
        int chunk_ms = DEFAULT_CHUNK_MS;

        int chunk_frames() const {
            int f = frames_per_ms_span(chunk_ms);
            return f > 1 ? f : 1;
        }

        int chunk_bytes() const {
            return chunk_frames() * CHANNELS * BYTES_PER_SAMPLE;
        }

        // 目標レイテンシ。リング + サーバ側キューの合計に対する目標。
        int target_frames() const {
            int f = frames_per_ms_span(latency_ms);
            int c = chunk_frames();
            return f > c ? f : c;
        }

        // リングに常に残しておく予備。1 チャンク分のノコギリを吸える程度。
        int min_ring_frames() const {
            return chunk_frames() * 2;
        }

        // 走り出す前にリングへ貯めておく量。予備より厚めに取る。
        // capture 側は最初の数チャンクの配送が不揃いで、予備ぎりぎりで開始すると
        // その揺れで即 underrun する。余った分はドリフト制御が数秒かけて均す。
        int start_ring_frames() const {
            return chunk_frames() * 4;
        }
    };

} // namespace acr
