#pragma once

#include <cstdint>
#include <string>

namespace acr {

    // capture source 1 件。PulseAudio の型はここに漏らさない。
    struct DeviceInfo {
        std::uint32_t index = 0;
        std::string name;
        std::string description;
        bool is_monitor = false;
        std::string monitor_of_sink_name;
        bool is_default = false;
    };

} // namespace acr
