#include "adapters/pulse_device_lister.h"

#include "domain/device_match.h"
#include "domain/text_util.h"

#include <pulse/pulseaudio.h>

#include <chrono>
#include <thread>

namespace acr {

    namespace {

        constexpr auto QUERY_TIMEOUT = std::chrono::seconds(3);
        constexpr auto QUERY_POLL_INTERVAL = std::chrono::milliseconds(1);

        // 1 回のクエリで使う PulseAudio のメインループとコンテキスト。
        // デストラクタで必ず片付ける。
        class QuerySession {
        public:
            ~QuerySession() { cleanup(); }

            bool connect(std::string& error_message) {
                mainloop_ = pa_mainloop_new();
                if (!mainloop_) {
                    error_message = "pa_mainloop_new failed";
                    return false;
                }

                api_ = pa_mainloop_get_api(mainloop_);
                context_ = pa_context_new(api_, "AudioCaptureRelay Source Query");
                if (!context_) {
                    error_message = "pa_context_new failed";
                    return false;
                }

                if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
                    error_message = std::string("pa_context_connect failed: ") + pa_strerror(pa_context_errno(context_));
                    return false;
                }

                int error = 0;
                const auto deadline = std::chrono::steady_clock::now() + QUERY_TIMEOUT;
                while (true) {
                    if (pa_mainloop_iterate(mainloop_, 0, &error) < 0) {
                        error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                        return false;
                    }

                    auto state = pa_context_get_state(context_);
                    if (state == PA_CONTEXT_READY) return true;
                    if (!PA_CONTEXT_IS_GOOD(state)) {
                        error_message = std::string("PulseAudio context failed: ") + pa_strerror(pa_context_errno(context_));
                        return false;
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        error_message = "PulseAudio connection timed out";
                        return false;
                    }
                    std::this_thread::sleep_for(QUERY_POLL_INTERVAL);
                }
            }

            pa_context* context() { return context_; }

            bool wait_operation(pa_operation* op, std::string& error_message) {
                if (!op) {
                    error_message = std::string("PulseAudio operation failed: ") + pa_strerror(pa_context_errno(context_));
                    return false;
                }

                int error = 0;
                const auto deadline = std::chrono::steady_clock::now() + QUERY_TIMEOUT;
                pa_operation_state_t state = pa_operation_get_state(op);
                while (state == PA_OPERATION_RUNNING) {
                    if (pa_mainloop_iterate(mainloop_, 0, &error) < 0) {
                        error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                        pa_operation_unref(op);
                        return false;
                    }
                    state = pa_operation_get_state(op);
                    if (state == PA_OPERATION_RUNNING) {
                        if (std::chrono::steady_clock::now() >= deadline) {
                            error_message = "PulseAudio operation timed out";
                            pa_operation_cancel(op);
                            pa_operation_unref(op);
                            return false;
                        }
                        std::this_thread::sleep_for(QUERY_POLL_INTERVAL);
                    }
                }

                pa_operation_unref(op);
                if (state != PA_OPERATION_DONE) {
                    error_message = std::string("PulseAudio operation was cancelled: ")
                                  + pa_strerror(pa_context_errno(context_));
                    return false;
                }
                return true;
            }

        private:
            void cleanup() {
                if (context_) {
                    pa_context_disconnect(context_);
                    pa_context_unref(context_);
                    context_ = nullptr;
                }
                if (mainloop_) {
                    pa_mainloop_free(mainloop_);
                    mainloop_ = nullptr;
                }
                api_ = nullptr;
            }

            pa_mainloop* mainloop_ = nullptr;
            pa_mainloop_api* api_ = nullptr;
            pa_context* context_ = nullptr;
        };

    } // namespace

    // コールバックから書き戻す先。C の userdata に渡すためだけの入れ物。
    struct PulseDeviceLister::Impl {
        std::vector<DeviceInfo>* sources = nullptr;
        std::vector<DeviceInfo>* sinks = nullptr;
        std::string* default_source = nullptr;
        std::string* default_sink = nullptr;
        bool callback_failed = false;

        static void server_info_cb(pa_context*, const pa_server_info* info, void* userdata) {
            auto* self = static_cast<Impl*>(userdata);
            if (!info) {
                self->callback_failed = true;
                return;
            }
            if (info->default_source_name) *self->default_source = info->default_source_name;
            if (info->default_sink_name) *self->default_sink = info->default_sink_name;
        }

        static void sink_info_cb(pa_context*, const pa_sink_info* info, int eol, void* userdata) {
            auto* self = static_cast<Impl*>(userdata);
            if (eol < 0) {
                self->callback_failed = true;
                return;
            }
            if (eol > 0 || !info) return;

            DeviceInfo s;
            s.index = info->index;
            s.name = safe(info->name);
            s.description = safe(info->description);
            self->sinks->push_back(std::move(s));
        }

        static void source_info_cb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
            auto* self = static_cast<Impl*>(userdata);
            if (eol < 0) {
                self->callback_failed = true;
                return;
            }
            if (eol > 0 || !info) return;

            DeviceInfo s;
            s.index = info->index;
            s.name = safe(info->name);
            s.description = safe(info->description);
            s.is_monitor = info->monitor_of_sink != PA_INVALID_INDEX;
            s.monitor_of_sink_name = safe(info->monitor_of_sink_name);
            self->sources->push_back(std::move(s));
        }
    };

    bool PulseDeviceLister::query(std::string& error_message) {
        sources_.clear();
        sinks_.clear();
        default_source_.clear();
        default_sink_.clear();

        QuerySession session;
        if (!session.connect(error_message)) return false;

        Impl out{&sources_, &sinks_, &default_source_, &default_sink_};

        auto operation_succeeded = [&](pa_operation* operation, const char* name) {
            if (!session.wait_operation(operation, error_message)) return false;
            if (!out.callback_failed) return true;

            error_message = std::string("PulseAudio ") + name + " callback failed: "
                          + pa_strerror(pa_context_errno(session.context()));
            return false;
        };

        out.callback_failed = false;
        if (!operation_succeeded(pa_context_get_server_info(session.context(), &Impl::server_info_cb, &out),
                                 "server-info")) {
            return false;
        }

        out.callback_failed = false;
        if (!operation_succeeded(pa_context_get_source_info_list(session.context(), &Impl::source_info_cb, &out),
                                 "source-list")) {
            return false;
        }

        out.callback_failed = false;
        if (!operation_succeeded(pa_context_get_sink_info_list(session.context(), &Impl::sink_info_cb, &out),
                                 "sink-list")) {
            return false;
        }

        for (auto& s : sources_) s.is_default = (s.name == default_source_);
        for (auto& s : sinks_) s.is_default = (s.name == default_sink_);

        sort_devices(sources_);
        sort_devices(sinks_);
        return true;
    }

} // namespace acr
