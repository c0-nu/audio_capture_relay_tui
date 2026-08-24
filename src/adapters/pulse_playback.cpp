#include "adapters/pulse_playback.h"

#include "domain/drift_control.h"
#include "domain/error_log.h"
#include "domain/output_mix.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace acr {

    namespace {

        // プライミングで流す無音の上限(安全弁)。実測では 40 チャンク前後で詰まる。
        constexpr int PREFILL_MAX_CHUNKS = 150;

        // capture 側と同じく、失敗が続いたら打ち切る。
        constexpr auto FAILURE_LIMIT = std::chrono::seconds(3);

    } // namespace

    void run_playback(SharedState& st, const RelayConfig& cfg) {
        pa_sample_spec ss{};
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = SAMPLE_RATE;
        ss.channels = CHANNELS;

        const int chunk_frames = cfg.chunk_frames();
        const int chunk_bytes = cfg.chunk_bytes();
        const int target_frames = cfg.target_frames();

        pa_buffer_attr play_attr{};
        // maxlength はサーバ側が「一度に飲める量」の上限。既定(2秒)のままだと、
        // 最初の数回の書き込みでプライムしたリングを丸ごと持って行かれ、リングが
        // 空になって起動直後に underrun が並ぶ。合計レイテンシで制御している以上、
        // サーバ側に深く積ませる意味は無いので、tlength の 2 倍で頭打ちにする。
        play_attr.maxlength = static_cast<uint32_t>(chunk_bytes * 4);
        // The target latency is enforced by our own ring buffer plus this queue,
        // so PulseAudio's own buffer just needs to hold a chunk or two.
        play_attr.tlength = static_cast<uint32_t>(chunk_bytes * 2);
        play_attr.prebuf = 0;
        play_attr.minreq = static_cast<uint32_t>(chunk_bytes);
        play_attr.fragsize = static_cast<uint32_t>(-1);

        int error = 0;
        pa_simple* play = pa_simple_new(
            nullptr,
            "AudioCaptureRelay",
            PA_STREAM_PLAYBACK,
            st.sink_name.empty() ? nullptr : st.sink_name.c_str(),
            "relay playback",
            &ss,
            nullptr,
            &play_attr,
            &error
        );

        if (!play) {
            st.abort(std::string("pa_simple_new(playback) failed: ") + pa_strerror(error));
            return;
        }

        std::vector<int16_t> output(static_cast<std::size_t>(chunk_frames) * CHANNELS);
        std::vector<int16_t> popped_buffer; // 毎チャンク確保し直さないよう使い回す
        popped_buffer.reserve(static_cast<std::size_t>(chunk_frames + 64) * CHANNELS);
        PaddingState pad; // 枯れたときの埋め方(チャンクをまたいで持ち越す)

        // サーバ側キューの滞留。取得に失敗したら直前の値を使う(毎チャンク聞くので
        // 1 回落ちても次で戻る。ここで stderr に吐くと TUI が壊れる)。
        std::int64_t downstream_frames = 0;
        auto query_downstream = [&]() {
            int lat_error = 0;
            pa_usec_t usec = pa_simple_get_latency(play, &lat_error);
            if (usec != static_cast<pa_usec_t>(-1)) {
                downstream_frames = static_cast<std::int64_t>(usec) * SAMPLE_RATE / 1000000;
            }
            return downstream_frames;
        };

        // --- プライミング ---
        // サーバ側は走り出しのしばらく「飲み放題」で、書き込みがまったくブロックしない
        // (実測で 700ms 以上。pa_buffer_attr の maxlength とは無関係で、この間に書いた
        // 分は latency にも出てこない)。ここへ実音声を流すとリングが数百 us で空になり、
        // 起動直後に underrun が並ぶ。なので:
        //
        //   1. 詰まる(= 書き込みがチャンク長で律速され始める)まで**無音**を流す
        //   2. リングを目標水位に合わせる。足りなければ待ち、多ければ削る
        //
        // 2 で削るのは中継開始前の音なので、落としても誰も困らない。
        const int ring_start_frames = std::max(target_frames, cfg.start_ring_frames());

        {
            const std::vector<int16_t> silence(static_cast<std::size_t>(chunk_frames) * CHANNELS, 0);
            const auto chunk_duration = std::chrono::microseconds(1000000LL * chunk_frames / SAMPLE_RATE);
            int slow_writes = 0;

            for (int i = 0; i < PREFILL_MAX_CHUNKS && st.is_running(); ++i) {
                auto started = std::chrono::steady_clock::now();
                if (pa_simple_write(play, silence.data(), silence.size() * sizeof(int16_t), &error) < 0) break;

                // 1 回の遅さはスケジューリングのぶれでも起きるので、2 回続けて見る。
                if (std::chrono::steady_clock::now() - started > chunk_duration / 2) {
                    if (++slow_writes >= 2) break;
                } else {
                    slow_writes = 0;
                }
            }
        }

        while (st.is_running() && st.ring.frames_buffered() < static_cast<std::size_t>(ring_start_frames)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        st.ring.trim(static_cast<std::size_t>(ring_start_frames));

        FailureWindow failures(FAILURE_LIMIT);

        DriftController drift(target_frames, chunk_frames, cfg.chunk_ms, cfg.min_ring_frames(),
                              static_cast<std::int64_t>(st.ring.frames_buffered()) + query_downstream());

        while (st.is_running()) {
            auto decision = drift.update(static_cast<std::int64_t>(st.ring.frames_buffered()), query_downstream());
            st.smoothed_total_frames.store(decision.smoothed_total_frames);
            st.downstream_frames.store(downstream_frames);
            st.reserve_hold.store(decision.reserve_hold);
            st.drift_ms.store(decision.drift_ms);

            auto popped = st.ring.pop(static_cast<std::size_t>(decision.consume_frames), popped_buffer);
            if (popped.underrun) st.underruns.fetch_add(1);

            assemble_output(output, popped_buffer, chunk_frames, pad);
            apply_volume(output, st.paused.load(), st.muted.load(), st.volume.load());

            if (pa_simple_write(play, output.data(), output.size() * sizeof(int16_t), &error) < 0) {
                st.errors.report(std::string("pa_simple_write failed: ") + pa_strerror(error));
                if (failures.record_failure(std::chrono::steady_clock::now())) {
                    st.abort("playback failed continuously, giving up");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            failures.record_success();
        }

        pa_simple_drain(play, &error);
        pa_simple_free(play);
    }

} // namespace acr
