#pragma once

#include "domain/device_info.h"

#include <string>
#include <vector>

namespace acr {

    // PulseAudio / PipeWire-Pulse に接続して capture source と出力 sink を一覧する。
    // ここが PulseAudio 型との境界。外へは DeviceInfo しか出さない。
    class PulseDeviceLister {
    public:
        // 失敗したら false を返し、error_message に理由を入れる(例外は投げない)。
        bool query(std::string& error_message);

        const std::vector<DeviceInfo>& sources() const { return sources_; }
        const std::vector<DeviceInfo>& sinks() const { return sinks_; }

    private:
        struct Impl;

        std::vector<DeviceInfo> sources_;
        std::vector<DeviceInfo> sinks_;
        std::string default_source_;
        std::string default_sink_;
    };

} // namespace acr
