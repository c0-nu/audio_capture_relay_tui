#include "domain/error_log.h"

namespace acr {

    void ErrorLog::report(const std::string& message) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            message_ = message;
        }
        count_.fetch_add(1);
    }

    ErrorLog::Snapshot ErrorLog::snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return Snapshot{message_, count_.load()};
    }

    bool FailureWindow::record_failure(std::chrono::steady_clock::time_point now) {
        if (!first_failure_) {
            first_failure_ = now;
            return false;
        }
        return (now - *first_failure_) >= limit_;
    }

} // namespace acr
