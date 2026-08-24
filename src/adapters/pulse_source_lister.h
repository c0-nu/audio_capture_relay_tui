#pragma once

#include "domain/source_info.h"

#include <string>
#include <vector>

namespace acr {

    // PulseAudio / PipeWire-Pulse に接続して capture source を一覧する。
    // ここが PulseAudio 型との境界。外へは SourceInfo しか出さない。
    class PulseSourceLister {
    public:
        // 失敗したら false を返し、error_message に理由を入れる(例外は投げない)。
        bool query(std::string& error_message);

        const std::vector<SourceInfo>& sources() const { return sources_; }

    private:
        struct Impl;

        std::vector<SourceInfo> sources_;
        std::string default_source_;
    };

} // namespace acr
