// Composition Root。
// ここだけが全レイヤを知っている: 引数を解析し、source を決め、共有状態を作り、
// capture / playback / 表示 を配線して回す。処理の中身はここに書かない。

#include "adapters/plain_status.h"
#include "adapters/pulse_capture.h"
#include "adapters/pulse_playback.h"
#include "adapters/pulse_device_lister.h"
#include "adapters/tui_ncurses.h"
#include "app/options.h"
#include "app/signal_handling.h"
#include "app/device_cli.h"
#include "domain/shared_state.h"

#include <iostream>
#include <optional>
#include <thread>

int main(int argc, char** argv) {
    auto parsed = acr::parse_args(argc, argv);
    if (!parsed.options) return parsed.exit_code;
    const acr::Options opt = *parsed.options;

    acr::PulseDeviceLister lister;
    std::string error;
    if (!lister.query(error)) {
        std::cerr << "Failed to query PulseAudio/PipeWire-Pulse sources: " << error << "\n";
        return 1;
    }

    const auto& sources = lister.sources();

    if (opt.list) {
        acr::print_sources(sources);
        acr::print_sinks(lister.sinks());
        return 0;
    }

    auto selected = acr::choose_source(sources, opt.source_arg, opt.interactive_select);
    if (!selected) {
        std::cerr << "\nUse --list to inspect available sources.\n";
        return 1;
    }

    bool sink_failed = false;
    std::optional<acr::DeviceInfo> sink;
    if (opt.relay_enabled) {
        sink = acr::choose_sink(lister.sinks(), opt.sink_arg, sink_failed);
    }
    if (sink_failed) {
        std::cerr << "\nUse --list to inspect available sinks.\n";
        return 1;
    }

    acr::SharedState st;
    if (sink) {
        st.sink_name = sink->name;
        st.sink_description = sink->description;
    }
    st.relay_enabled = opt.relay_enabled;
    st.source_name = selected->name;
    st.source_description = selected->description;
    st.source_is_monitor = selected->is_monitor;
    st.volume.store(opt.volume);
    st.waveform_enabled.store(opt.waveform);
    st.waveform_style.store(opt.waveform_style);

    acr::install_signal_handlers(st);

    if (!opt.tui) {
        std::cout << "AudioCaptureRelay\n"
        << "  source: " << st.source_description << "\n"
        << "  name:   " << st.source_name << "\n"
        << "  type:   " << (st.source_is_monitor ? "MONITOR/output-capture" : "INPUT") << "\n"
        << "  output: " << (st.relay_enabled
                              ? (st.sink_description.empty() ? "(default sink)" : st.sink_description)
                              : "(none: --no-relay)") << "\n"
        << "Press Ctrl+C to stop.\n";
    }

    std::thread capture(acr::run_capture, std::ref(st), std::cref(opt.relay));

    // --no-relay では再生ストリームを作らない。スレッドごと立てないので、
    // pavucontrol にも出ないし、ドリフト補正も動かない。
    std::thread playback;
    if (opt.relay_enabled) {
        playback = std::thread(acr::run_playback, std::ref(st), std::cref(opt.relay));
    }

    if (opt.tui) acr::run_tui(st, opt.relay);
    else acr::run_plain_status(st);

    st.request_stop();
    if (capture.joinable()) capture.join();
    if (playback.joinable()) playback.join();

    // TUI 中は画面を壊さないよう stderr に出していないので、閉じたあとに出す。
    auto err = st.errors.snapshot();
    if (err.count > 0) {
        std::cerr << "Last error (" << err.count << " total): " << err.message << "\n";
    }

    return st.aborted.load() ? 1 : 0;
}
