#pragma once

#include "domain/shared_state.h"

namespace acr {

    // --no-tui のときの 1 行ステータス表示。st.running が false になるまで戻らない。
    void run_plain_status(SharedState& st);

} // namespace acr
