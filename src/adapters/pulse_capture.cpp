#include "adapters/pulse_capture.h"

#include "domain/error_log.h"
#include "domain/level_meter.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <chrono>
#include <thread>
#include <vector>

namespace acr {

    namespace {

        // Safety net: if playback stalls entirely (device unplugged, etc.) the
        // ring buffer must not grow without bound. Cap it well above the target
        // latency; the drift correction in run_playback keeps it near target
        // during normal operation, this is just a backstop. It's crossfaded on
        // the off chance it does fire, so it doesn't itself produce a click.
        constexpr std::size_t MAX_RING_FRAMES = static_cast<std::size_t>(SAMPLE_RATE) * 3; // 3s hard cap

        // これだけ失敗し続けたら復帰の見込みなしとみて打ち切る(source が
        // 消えたまま 10ms ごとに永遠にリトライしない)。
        constexpr auto FAILURE_LIMIT = std::chrono::seconds(3);

    } // namespace

    void run_capture(SharedState& st, const RelayConfig& cfg) {
        pa_sample_spec ss{};
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = SAMPLE_RATE;
        ss.channels = CHANNELS;

        const int chunk_frames = cfg.chunk_frames();

        pa_buffer_attr rec_attr{};
        rec_attr.maxlength = static_cast<uint32_t>(-1);
        rec_attr.tlength = static_cast<uint32_t>(-1);
        rec_attr.prebuf = static_cast<uint32_t>(-1);
        rec_attr.minreq = static_cast<uint32_t>(-1);
        rec_attr.fragsize = static_cast<uint32_t>(cfg.chunk_bytes());

        int error = 0;
        pa_simple* rec = pa_simple_new(
            nullptr,
            "AudioCaptureRelay",
            PA_STREAM_RECORD,
            st.source_name.empty() ? nullptr : st.source_name.c_str(),
            "capture",
            &ss,
            nullptr,
            &rec_attr,
            &error
        );

        if (!rec) {
            st.abort(std::string("pa_simple_new(record) failed: ") + pa_strerror(error));
            return;
        }

        std::vector<int16_t> input(static_cast<std::size_t>(chunk_frames) * CHANNELS);
        ChunkAnalysis analysis; // 毎チャンク作り直さず使い回す
        FailureWindow failures(FAILURE_LIMIT);

        while (st.is_running()) {
            if (pa_simple_read(rec, input.data(), input.size() * sizeof(int16_t), &error) < 0) {
                st.errors.report(std::string("pa_simple_read failed: ") + pa_strerror(error));
                if (failures.record_failure(std::chrono::steady_clock::now())) {
                    st.abort("capture failed continuously, giving up");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            failures.record_success();

            float vol = st.muted.load() ? 0.0f : st.volume.load();
            float analysis_vol = st.paused.load() ? 0.0f : vol;

            analyze_chunk(input, static_cast<std::size_t>(chunk_frames), analysis_vol, analysis);
            st.rms_l.store(analysis.rms_l);
            st.rms_r.store(analysis.rms_r);
            st.peak_l.store(analysis.peak_l);
            st.peak_r.store(analysis.peak_r);
            st.clip_ratio.store(analysis.clip_ratio);
            st.wave.append(analysis.mono);

            auto pushed = st.ring.push(input, MAX_RING_FRAMES);
            if (pushed.trimmed) st.overflow_trims.fetch_add(1);

            st.frames_captured.fetch_add(static_cast<std::uint64_t>(chunk_frames));
        }

        pa_simple_free(rec);
    }

} // namespace acr
