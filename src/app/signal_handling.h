#pragma once

#include "domain/shared_state.h"

namespace acr {

    // SIGINT / SIGTERM を受けたら st.request_stop() を呼ぶようにする。
    // シグナルハンドラから触れる必要があるため、対象の参照はここ(Composition Root)が持つ。
    // 呼べるのはプロセスで 1 回だけ。
    void install_signal_handlers(SharedState& st);

} // namespace acr
