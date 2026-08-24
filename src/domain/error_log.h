#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace acr {

    // 直近のエラーと発生回数。どのスレッドからでも積める。
    // 表示は TUI / 標準出力のどちらか(= 表示層)の仕事なので、ここでは出力しない。
    class ErrorLog {
    public:
        struct Snapshot {
            std::string message;
            std::uint64_t count = 0;
        };

        void report(const std::string& message);

        Snapshot snapshot() const;
        std::uint64_t count() const { return count_.load(); }

    private:
        mutable std::mutex mutex_;
        std::string message_;
        std::atomic<std::uint64_t> count_{0};
    };

    // 「失敗が続いている時間」を見る。1 回の失敗では諦めず、復帰の見込みが無く
    // なったところで打ち切るために使う(source が消えたまま無限リトライしない)。
    class FailureWindow {
    public:
        explicit FailureWindow(std::chrono::steady_clock::duration limit) : limit_(limit) {}

        // 失敗を記録する。連続失敗が limit を超えていたら true(= 諦めどき)。
        bool record_failure(std::chrono::steady_clock::time_point now);

        void record_success() { first_failure_.reset(); }

    private:
        std::chrono::steady_clock::duration limit_;
        std::optional<std::chrono::steady_clock::time_point> first_failure_;
    };

} // namespace acr
