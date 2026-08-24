#pragma once

#include "domain/relay_config.h"

#include <optional>
#include <string>

namespace acr {

    // コマンドライン引数の解析結果。
    struct Options {
        bool list = false;                // --list
        bool tui = true;                  // --no-tui で false
        bool waveform = true;             // --no-waveform で false(波形表示の初期値)
        bool interactive_select = false;  // --select
        std::string source_arg;           // --source の値
        float volume = 1.0f;              // --volume(0.0〜4.0)
        RelayConfig relay;                // --latency-ms / --chunk-ms
    };

    void print_usage(const char* argv0);

    // --help / 引数エラーのときは nullopt(メッセージは出力済み)。
    std::optional<Options> parse_args(int argc, char** argv);

} // namespace acr
