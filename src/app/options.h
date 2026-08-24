#pragma once

#include "domain/relay_config.h"
#include "domain/waveform.h"

#include <optional>
#include <string>

namespace acr {

    // コマンドライン引数の解析結果。
    struct Options {
        bool list = false;                // --list
        bool relay_enabled = true;        // --no-relay で false(取り込んで表示するだけ)
        bool tui = true;                  // --no-tui で false
        bool waveform = true;             // --no-waveform で false(波形表示の初期値)
        WaveformStyle waveform_style = WaveformStyle::Envelope; // --waveform-style
        bool interactive_select = false;  // --select
        std::string source_arg;           // --source の値
        std::string sink_arg;             // --sink の値(空なら既定の sink)
        float volume = 1.0f;              // --volume(0.0〜4.0)
        RelayConfig relay;                // --latency-ms / --chunk-ms
    };

    void print_usage(const char* argv0);
    void print_version();

    // 引数解析の結果。--help / --version は「走らせない」だけで正常終了なので、
    // エラー(終了コード 2)と区別できるように終了コードも一緒に返す。
    struct ParsedArgs {
        std::optional<Options> options;  // 走らせるなら値が入る
        int exit_code = 0;               // options が空のときに main が返す値
    };

    // メッセージは parse_args の中で出力済み。
    ParsedArgs parse_args(int argc, char** argv);

} // namespace acr
