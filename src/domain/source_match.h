#pragma once

#include "domain/source_info.h"

#include <optional>
#include <string>
#include <vector>

namespace acr {

    // --source の引数から source を選ぶ。純粋関数 —— メッセージ出力は呼び出し側の仕事。
    struct SourceMatch {
        enum class Status {
            Ok,
            Empty,     // source が 1 件も無い
            NoMatch,   // どれにも一致しない
            Ambiguous  // 部分一致が複数
        };

        Status status = Status::Empty;
        std::optional<SourceInfo> source;
        std::vector<SourceInfo> candidates; // Ambiguous のときの一致リスト
    };

    // 優先順: 空引数ならデフォルト -> 番号 -> 名前/説明の完全一致 -> 部分一致。
    SourceMatch match_source(const std::vector<SourceInfo>& sources, const std::string& source_arg);

    // 並び順: デフォルトが先頭、次に INPUT、その後 MONITOR。同種内は説明で昇順。
    void sort_sources(std::vector<SourceInfo>& sources);

} // namespace acr
