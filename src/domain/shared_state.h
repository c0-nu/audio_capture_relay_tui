#pragma once

#include "domain/audio_format.h"
#include "domain/error_log.h"
#include "domain/pcm_ring.h"
#include "domain/wave_history.h"
#include "domain/waveform.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace acr {

    // スレッド間で共有する可変状態。
    // 所有するのは main(Composition Root)。capture / playback / 表示 の各スレッドは
    // 参照だけを受け取る。読み書きは atomic か、内部にロックを持つ型越し。
    struct SharedState {
        // 実行中フラグ。false になったら全スレッドが自分で抜ける。
        std::atomic<bool> running{true};

        // 続行不能で止めたか。ユーザーが q / Ctrl+C で止めた場合は false のまま。
        std::atomic<bool> aborted{false};

        bool is_running() const { return running.load(); }
        void request_stop() { running.store(false); }

        void abort(const std::string& message) {
            errors.report(message);
            aborted.store(true);
            running.store(false);
        }

        // 波形表示用の履歴(モノラルミックス)。
        WaveHistory wave{WAVE_BUCKET_SAMPLES, WAVE_HISTORY_BUCKETS};

        // capture(producer)と playback(consumer)の間のリングバッファ。
        PcmRing ring;

        // 生のリング水位は ring.frames_buffered() が正。ここには置かない(二重管理を避ける)。
        std::atomic<std::int64_t> smoothed_total_frames{0};    // 平滑後の合計滞留(表示用)
        std::atomic<std::int64_t> downstream_frames{0};        // サーバ側キューの滞留(表示用)
        std::atomic<std::int64_t> effective_target_frames{0};  // 実際に狙っている水位
        std::atomic<bool> raised_target{false};                // 要求値では届かず押し上げた
        std::atomic<std::int64_t> drift_ms{0};                 // signed: +too much buffered, -starving
        std::atomic<std::uint64_t> underruns{0};               // times playback ran out of buffered audio
        // 枯れてはいないが、ドリフト補正で 1 チャンク未満しか消費せず埋めた回数。
        // underruns には出ないので、これが無いと「健全に見えるのに音が削れる」状態に気付けない。
        std::atomic<std::uint64_t> pads{0};
        std::atomic<std::uint64_t> overflow_trims{0};          // times the ring buffer hit its safety-net cap

        std::atomic<float> volume{1.0f};
        std::atomic<bool> muted{false};
        std::atomic<bool> waveform_enabled{true};
        std::atomic<WaveformStyle> waveform_style{WaveformStyle::Envelope};
        std::atomic<bool> paused{false};

        std::atomic<float> rms_l{0.0f};
        std::atomic<float> rms_r{0.0f};
        std::atomic<float> peak_l{0.0f};
        std::atomic<float> peak_r{0.0f};
        std::atomic<float> clip_ratio{0.0f};
        std::atomic<std::uint64_t> frames_captured{0};

        // 直近のエラー。出力は表示層に任せる(TUI 中に stderr へ書くと画面が壊れる)。
        ErrorLog errors;

        // 起動時に一度だけ書き、以降は読むだけ。
        std::string source_name;
        std::string source_description;
        bool source_is_monitor = false;

        // 出力先。空なら PulseAudio の既定 sink に任せる。
        std::string sink_name;
        std::string sink_description;
    };

} // namespace acr
