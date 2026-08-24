// Composition Root。
// ここだけが全レイヤを知っている: 引数を解析し、source を決め、共有状態を作り、
// capture / playback / 表示 を配線して回す。処理の中身はここに書かない。

#include "adapters/plain_status.h"
#include "adapters/pulse_capture.h"
#include "adapters/pulse_playback.h"
#include "adapters/pulse_source_lister.h"
#include "adapters/tui_ncurses.h"
#include "app/options.h"
#include "app/signal_handling.h"
#include "app/source_cli.h"
#include "domain/shared_state.h"

#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    auto parsed = acr::parse_args(argc, argv);
    if (!parsed) return 1;
    const acr::Options opt = *parsed;

    acr::PulseSourceLister lister;
    std::string error;
    if (!lister.query(error)) {
        std::cerr << "Failed to query PulseAudio/PipeWire-Pulse sources: " << error << "\n";
        return 1;
    }

    const auto& sources = lister.sources();

    if (opt.list) {
        acr::print_sources(sources);
        return 0;
    }

    auto selected = acr::choose_source(sources, opt.source_arg, opt.interactive_select);
    if (!selected) {
        std::cerr << "\nUse --list to inspect available sources.\n";
        return 1;
    }

    acr::SharedState st;
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
        << "Press Ctrl+C to stop.\n";
    }

    std::thread capture(acr::run_capture, std::ref(st), std::cref(opt.relay));
    std::thread playback(acr::run_playback, std::ref(st), std::cref(opt.relay));

    if (opt.tui) acr::run_tui(st, opt.relay);
    else acr::run_plain_status(st);

    st.request_stop();
    if (capture.joinable()) capture.join();
    if (playback.joinable()) playback.join();

    return 0;
}
