#pragma once

#include "domain/relay_config.h"
#include "domain/shared_state.h"

namespace acr {

    // capture source を読み続け、解析結果とリングバッファへ書き込む。
    // st.running が false になるまで戻らない。専用スレッドで動かす。
    void run_capture(SharedState& st, const RelayConfig& cfg);

} // namespace acr
