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

        // サーバ側キューに持たせる上限(チャンク数)。書き込みが実時間より
        // これ以上先行しないよう自分で待つ = サーバ側の滞留がここで頭打ちになる。
        //
        // サーバは走り出しに「飲み放題」の時期があり、pa_buffer_attr では止まらない
        // (実測: 目標 1000ms でリングに貯めた 980ms が一瞬で 111ms まで持って行かれた)。
        // 書く側で実時間ペースを守れば、目標がいくつでもこれが起きない。
        constexpr int PACE_SLACK_CHUNKS = 4;

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
        popped_buffer.reserve(static_cast<std::size_t>(
            chunk_frames + max_drift_correction_frames(chunk_frames)) * CHANNELS);
        PaddingState pad; // 枯れたときの埋め方(チャンクをまたいで持ち越す)

        FailureWindow failures(FAILURE_LIMIT);

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

        // 実時間ペースを守る。書き込みが実時間より pace_slack 以上先行しないよう待つ。
        // これがそのまま「サーバ側に持たせる量の上限」になる。
        const auto chunk_duration = std::chrono::microseconds(1000000LL * chunk_frames / SAMPLE_RATE);
        const auto pace_slack = chunk_duration * PACE_SLACK_CHUNKS;

        const int slack_frames = chunk_frames * PACE_SLACK_CHUNKS;

        std::uint64_t written_frames = 0;
        auto stream_start = std::chrono::steady_clock::now();

        auto pace = [&]() {
            // サーバ側の滞留が実測できていて、しかも浅いなら、先行していない。
            // その時点を基準に取り直す —— 壁時計とオーディオクロックは少しずつ
            // ずれるので、絶対スケジュールを持ち続けると何十分か後に「先行して
            // いないのに待つ」ようになり、サーバ側を枯らしてしまう。
            const std::int64_t out = query_downstream();
            if (out > 0 && out <= slack_frames) {
                stream_start = std::chrono::steady_clock::now();
                written_frames = static_cast<std::uint64_t>(out);
                return;
            }

            // 実測できない(走り出しは 0 を返す)か、深すぎる場合は壁時計で抑える。
            const auto scheduled = stream_start
            + std::chrono::microseconds(1000000LL * static_cast<long long>(written_frames) / SAMPLE_RATE)
            - pace_slack;
            const auto now = std::chrono::steady_clock::now();
            if (now < scheduled) std::this_thread::sleep_for(scheduled - now);
        };

        // --- プライミング ---
        // リングが目標水位に達するまで、**無音を流し続けながら**待つ。
        //   - 実音声で埋めるとリングから取られる
        //   - かといって何も書かずに待つと、待っている間にサーバ側が枯れる
        //     (目標が大きいほど待ちが長い。実測で out が 0 に張り付いた)
        // リングの持ち分は「目標 - サーバ側に持たせる分」。ペースを守る限り
        // サーバ側は pace_slack で頭打ちなので、get_latency を待たずに決められる。
        const int ring_start_frames = std::max(cfg.start_ring_frames(), target_frames - slack_frames);

        {
            const std::vector<int16_t> silence(static_cast<std::size_t>(chunk_frames) * CHANNELS, 0);
            while (st.is_running() && st.ring.frames_buffered() < static_cast<std::size_t>(ring_start_frames)) {
                if (pa_simple_write(play, silence.data(), silence.size() * sizeof(int16_t), &error) < 0) {
                    st.errors.report(std::string("pa_simple_write(priming) failed: ") + pa_strerror(error));
                    failures.record_failure(std::chrono::steady_clock::now());
                    break;
                }
                written_frames += static_cast<std::uint64_t>(chunk_frames);
                pace();
            }
        }

        // 貯まりすぎた分は捨てる(中継開始前の音なので落としてよい)。
        st.ring.trim(static_cast<std::size_t>(ring_start_frames));

        DriftController drift(target_frames, chunk_frames, cfg.chunk_ms, cfg.min_ring_frames(),
                              static_cast<std::int64_t>(st.ring.frames_buffered()), query_downstream());

        while (st.is_running()) {
            auto decision = drift.update(static_cast<std::int64_t>(st.ring.frames_buffered()), query_downstream());
            st.smoothed_total_frames.store(decision.smoothed_total_frames);
            st.downstream_frames.store(downstream_frames);
            st.effective_target_frames.store(decision.effective_target_frames);
            st.raised_target.store(decision.raised_target);
            st.drift_ms.store(decision.drift_ms);

            auto popped = st.ring.pop(static_cast<std::size_t>(decision.consume_frames), popped_buffer);
            if (popped.underrun) st.underruns.fetch_add(1);

            const auto fill = assemble_output(output, popped_buffer, chunk_frames, pad);
            // 枯れていないのに埋めた = ドリフト補正が 1 チャンク未満しか消費しなかった。
            if (!popped.underrun && fill.padded_frames > 0) st.pads.fetch_add(1);
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
            written_frames += static_cast<std::uint64_t>(chunk_frames);
            pace();
        }

        // 終了時にサーバ側キューの再生完了を待つ必要はない。drain は接続異常時に
        // 停止を長引かせるため、ここでは残りを捨てて即時に閉じる。
        pa_simple_free(play);
    }

} // namespace acr
