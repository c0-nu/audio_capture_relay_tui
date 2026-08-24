#pragma once

#include "domain/relay_config.h"
#include "domain/shared_state.h"

namespace acr {

    // ncurses の全画面 TUI。キー入力もここで拾う。
    // st.running が false になるまで戻らない(呼び出しスレッドを占有する)。
    void run_tui(SharedState& st, const RelayConfig& cfg);

} // namespace acr
