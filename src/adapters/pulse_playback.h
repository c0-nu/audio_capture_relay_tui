#pragma once

#include "domain/relay_config.h"
#include "domain/shared_state.h"

namespace acr {

    // リングバッファから取り出して、自分自身の再生ストリームへ書き続ける。
    // 目標レイテンシに保つための消費量調整は DriftController が決める。
    // st.running が false になるまで戻らない。専用スレッドで動かす。
    void run_playback(SharedState& st, const RelayConfig& cfg);

} // namespace acr
