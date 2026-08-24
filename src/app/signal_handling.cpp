#include "app/signal_handling.h"

#include <csignal>

namespace acr {

    namespace {

        // シグナルハンドラから触れるのは atomic だけに限る。
        std::atomic<bool>* g_stop_target = nullptr;

        void on_signal(int) {
            if (g_stop_target) g_stop_target->store(false);
        }

    } // namespace

    void install_signal_handlers(SharedState& st) {
        g_stop_target = &st.running;
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
    }

} // namespace acr
