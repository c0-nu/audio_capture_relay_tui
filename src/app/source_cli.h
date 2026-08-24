#pragma once

#include "domain/source_info.h"

#include <optional>
#include <string>
#include <vector>

namespace acr {

    // source 一覧の表示と、選択結果の説明メッセージ。ここが標準入出力の担当。
    void print_sources(const std::vector<SourceInfo>& sources);

    // interactive なら一覧を出して番号を訊く。そうでなければ source_arg で選ぶ。
    // 選べなければ理由を stderr に出して nullopt。
    std::optional<SourceInfo> choose_source(const std::vector<SourceInfo>& sources,
                                            const std::string& source_arg,
                                            bool interactive);

} // namespace acr
