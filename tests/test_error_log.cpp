#include "domain/error_log.h"

#include <catch2/catch_test_macros.hpp>

using namespace acr;
using namespace std::chrono_literals;

TEST_CASE("ErrorLog は直近のメッセージと件数を持つ", "[error]") {
    ErrorLog log;
    CHECK(log.count() == 0);
    CHECK(log.snapshot().message.empty());

    log.report("first");
    log.report("second");

    auto s = log.snapshot();
    CHECK(s.count == 2);
    CHECK(s.message == "second"); // 直近が残る
    CHECK(log.count() == 2);
}

TEST_CASE("FailureWindow は 1 回の失敗では諦めない", "[error]") {
    auto t0 = std::chrono::steady_clock::time_point{};
    FailureWindow w(3s);

    CHECK_FALSE(w.record_failure(t0));
    CHECK_FALSE(w.record_failure(t0 + 1s));
    CHECK_FALSE(w.record_failure(t0 + 2999ms));
    CHECK(w.record_failure(t0 + 3s));       // 連続 3 秒で打ち切り
    CHECK(w.record_failure(t0 + 10s));
}

TEST_CASE("成功したら失敗の連続は切れる", "[error]") {
    auto t0 = std::chrono::steady_clock::time_point{};
    FailureWindow w(3s);

    CHECK_FALSE(w.record_failure(t0));
    CHECK_FALSE(w.record_failure(t0 + 2s));

    w.record_success();

    CHECK_FALSE(w.record_failure(t0 + 4s));  // ここから数え直し
    CHECK_FALSE(w.record_failure(t0 + 6s));
    CHECK(w.record_failure(t0 + 7s));
}
