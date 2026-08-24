#include "adapters/pulse_playback.h"

#include "domain/drift_control.h"
#include "domain/splice.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace acr {

    namespace {

        // プライミングで流す無音の上限(安全弁)。実測では 40 チャンク前後で詰まる。
        constexpr int PREFILL_MAX_CHUNKS = 150;

        // popped の先頭 chunk_frames を output へ。余分に取れていた分は、
        // 捨て際をクロスフェードしてから捨てる(スプライスのクリック対策)。
        void fill_from_popped(std::vector<int16_t>& output,
                              const std::vector<int16_t>& popped,
                              int chunk_frames,
                              int16_t& last_frame_l,
                              int16_t& last_frame_r) {
            const std::size_t chunk_samples = static_cast<std::size_t>(chunk_frames) * CHANNELS;
            const std::size_t popped_frames = popped.size() / CHANNELS;

            if (popped_frames >= static_cast<std::size_t>(chunk_frames)) {
                // Normal case (or catch-up after starving): use the first
                // chunk_frames worth of what we popped.
                std::copy(popped.begin(), popped.begin() + static_cast<std::ptrdiff_t>(chunk_samples), output.begin());

                if (popped_frames > static_cast<std::size_t>(chunk_frames)) {
                    // We deliberately over-consumed to drain excess buffer.
                    // Crossfade the boundary so discarding the extra frames
                    // doesn't produce an audible click/splice. The fade length
                    // must not exceed how many "extra" (dropped) frames we
                    // actually have on hand, or we'd read past the end of
                    // `popped`.
                    std::size_t extra_frames = popped_frames - static_cast<std::size_t>(chunk_frames);
                    std::size_t fade = std::min<std::size_t>({static_cast<std::size_t>(SPLICE_FADE_FRAMES),
                        static_cast<std::size_t>(chunk_frames),
                                                             extra_frames});
                    crossfade_tail(output.data() + (static_cast<std::size_t>(chunk_frames) - fade) * CHANNELS,
                                   popped.data() + chunk_samples,
                                   fade,
                                   CHANNELS);
                }
            } else {
                // Buffer is starving: fewer frames were available than a full
                // chunk. Copy what we have and pad the remainder by holding the
                // last real sample. Held for only a handful of samples this is
                // inaudible, and it buys the capture side time to catch up
                // instead of producing a hard dropout.
                if (!popped.empty()) std::copy(popped.begin(), popped.end(), output.begin());
                for (std::size_t f = popped_frames; f < static_cast<std::size_t>(chunk_frames); ++f) {
                    output[f * CHANNELS + 0] = last_frame_l;
                    output[f * CHANNELS + 1] = last_frame_r;
                }
            }

            if (chunk_frames > 0) {
                last_frame_l = output[static_cast<std::size_t>(chunk_frames - 1) * CHANNELS + 0];
                last_frame_r = output[static_cast<std::size_t>(chunk_frames - 1) * CHANNELS + 1];
            }
        }

        void apply_volume(std::vector<int16_t>& output, bool paused, bool muted, float volume) {
            if (paused) {
                std::fill(output.begin(), output.end(), 0);
                return;
            }

            float vol = muted ? 0.0f : volume;
            for (auto& s : output) {
                float v = static_cast<float>(s) * vol;
                v = std::clamp(v, -32768.0f, 32767.0f);
                s = static_cast<int16_t>(std::lround(v));
            }
        }

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
            nullptr,
            "relay playback",
            &ss,
            nullptr,
            &play_attr,
            &error
        );

        if (!play) {
            std::cerr << "pa_simple_new(playback) failed: " << pa_strerror(error) << "\n";
            st.request_stop();
            return;
        }

        std::vector<int16_t> output(static_cast<std::size_t>(chunk_frames) * CHANNELS);
        int16_t last_frame_l = 0, last_frame_r = 0; // for sample-and-hold padding on underrun

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

        DriftController drift(target_frames, chunk_frames, cfg.chunk_ms, cfg.min_ring_frames(),
                              static_cast<std::int64_t>(st.ring.frames_buffered()) + query_downstream());

        while (st.is_running()) {
            auto decision = drift.update(static_cast<std::int64_t>(st.ring.frames_buffered()), query_downstream());
            st.smoothed_total_frames.store(decision.smoothed_total_frames);
            st.downstream_frames.store(downstream_frames);
            st.reserve_hold.store(decision.reserve_hold);
            st.drift_ms.store(decision.drift_ms);

            auto popped = st.ring.pop(static_cast<std::size_t>(decision.consume_frames));
            if (popped.underrun) st.underruns.fetch_add(1);

            fill_from_popped(output, popped.samples, chunk_frames, last_frame_l, last_frame_r);
            apply_volume(output, st.paused.load(), st.muted.load(), st.volume.load());

            if (pa_simple_write(play, output.data(), output.size() * sizeof(int16_t), &error) < 0) {
                std::cerr << "pa_simple_write failed: " << pa_strerror(error) << "\n";
                st.errors.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        pa_simple_drain(play, &error);
        pa_simple_free(play);
    }

} // namespace acr
