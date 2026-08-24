#include "adapters/plain_status.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace acr {

    void run_plain_status(SharedState& st) {
        while (st.is_running()) {
            std::cout << "\r"
            << "vol=" << std::fixed << std::setprecision(0) << st.volume.load() * 100.0f << "%"
            << (st.muted.load() ? " muted" : "      ")
            << " peakL=" << std::setprecision(2) << st.peak_l.load()
            << " peakR=" << std::setprecision(2) << st.peak_r.load()
            << " frames=" << st.frames_captured.load()
            << " errors=" << st.errors.load()
            << " lat=" << std::setprecision(0)
            << frames_to_ms(static_cast<double>(st.smoothed_total_frames.load())) << "ms"
            << "(ring " << frames_to_ms(static_cast<double>(st.ring.frames_buffered()))
            << "/out " << frames_to_ms(static_cast<double>(st.downstream_frames.load())) << ")"
            << " drift=" << st.drift_ms.load() << "ms"
            << " underruns=" << st.underruns.load()
            << " overflow_trims=" << st.overflow_trims.load()
            << "        "
            << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        std::cout << "\n";
    }

} // namespace acr
