#include "adapters/plain_status.h"

#include <chrono>
#include <iomanip>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

namespace acr {

    void run_plain_status(SharedState& st) {
        std::uint64_t shown_errors = 0;

        while (st.is_running()) {
            // 新しく出たエラーだけ出す(毎回出すとステータス行が流れてしまう)。
            auto err = st.errors.snapshot();
            if (err.count > shown_errors) {
                std::cerr << "\n[error] " << err.message << "\n";
                shown_errors = err.count;
            }

            std::cout << "\r"
            << "vol=" << std::fixed << std::setprecision(0) << st.volume.load() * 100.0f << "%"
            << (st.muted.load() ? " muted" : "      ")
            << " peakL=" << std::setprecision(2) << st.peak_l.load()
            << " peakR=" << std::setprecision(2) << st.peak_r.load()
            << " frames=" << st.frames_captured.load()
            << " errors=" << st.errors.count()
            << " lat=" << std::setprecision(0)
            << frames_to_ms(static_cast<double>(st.smoothed_total_frames.load())) << "ms"
            << "(ring " << frames_to_ms(static_cast<double>(st.ring.frames_buffered()))
            << "/out " << frames_to_ms(static_cast<double>(st.downstream_frames.load())) << ")"
            << (st.raised_target.load()
                ? " floor=" + std::to_string(std::lround(frames_to_ms(static_cast<double>(st.effective_target_frames.load())))) + "ms"
                : "")
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
