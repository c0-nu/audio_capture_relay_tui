#pragma once

#include "domain/device_info.h"

#include <optional>
#include <string>
#include <vector>

namespace acr {

    // 一覧の表示と、選択結果の説明メッセージ。ここが標準入出力の担当。
    void print_sources(const std::vector<DeviceInfo>& sources);
    void print_sinks(const std::vector<DeviceInfo>& sinks);

    // interactive なら一覧を出して番号を訊く。そうでなければ source_arg で選ぶ。
    // 選べなければ理由を stderr に出して nullopt。
    std::optional<DeviceInfo> choose_source(const std::vector<DeviceInfo>& sources,
                                            const std::string& source_arg,
                                            bool interactive);

    // --sink の解決。arg が空なら「既定の sink に任せる」= nullopt を返し、
    // found を false のままにする(選べなかったのか、指定が無かったのかを区別する)。
    std::optional<DeviceInfo> choose_sink(const std::vector<DeviceInfo>& sinks,
                                          const std::string& sink_arg,
                                          bool& failed);

} // namespace acr
