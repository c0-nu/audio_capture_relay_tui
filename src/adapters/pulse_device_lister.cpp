#include "adapters/pulse_device_lister.h"

#include "domain/device_match.h"
#include "domain/text_util.h"

#include <pulse/pulseaudio.h>

namespace acr {

    namespace {

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
                while (true) {
                    if (pa_mainloop_iterate(mainloop_, 1, &error) < 0) {
                        error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                        return false;
                    }

                    auto state = pa_context_get_state(context_);
                    if (state == PA_CONTEXT_READY) return true;
                    if (!PA_CONTEXT_IS_GOOD(state)) {
                        error_message = std::string("PulseAudio context failed: ") + pa_strerror(pa_context_errno(context_));
                        return false;
                    }
                }
            }

            pa_context* context() { return context_; }

            bool wait_operation(pa_operation* op, std::string& error_message) {
                if (!op) {
                    error_message = std::string("PulseAudio operation failed: ") + pa_strerror(pa_context_errno(context_));
                    return false;
                }

                int error = 0;
                while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                    if (pa_mainloop_iterate(mainloop_, 1, &error) < 0) {
                        error_message = std::string("pa_mainloop_iterate failed: ") + pa_strerror(error);
                        pa_operation_unref(op);
                        return false;
                    }
                }

                pa_operation_unref(op);
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

        static void server_info_cb(pa_context*, const pa_server_info* info, void* userdata) {
            auto* self = static_cast<Impl*>(userdata);
            if (!info) return;
            if (info->default_source_name) *self->default_source = info->default_source_name;
            if (info->default_sink_name) *self->default_sink = info->default_sink_name;
        }

        static void sink_info_cb(pa_context*, const pa_sink_info* info, int eol, void* userdata) {
            if (eol > 0 || !info) return;
            auto* self = static_cast<Impl*>(userdata);

            DeviceInfo s;
            s.index = info->index;
            s.name = safe(info->name);
            s.description = safe(info->description);
            self->sinks->push_back(std::move(s));
        }

        static void source_info_cb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
            if (eol > 0 || !info) return;
            auto* self = static_cast<Impl*>(userdata);

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

        if (!session.wait_operation(pa_context_get_server_info(session.context(), &Impl::server_info_cb, &out), error_message)) {
            return false;
        }

        if (!session.wait_operation(pa_context_get_source_info_list(session.context(), &Impl::source_info_cb, &out), error_message)) {
            return false;
        }

        if (!session.wait_operation(pa_context_get_sink_info_list(session.context(), &Impl::sink_info_cb, &out), error_message)) {
            return false;
        }

        for (auto& s : sources_) s.is_default = (s.name == default_source_);
        for (auto& s : sinks_) s.is_default = (s.name == default_sink_);

        sort_devices(sources_);
        sort_devices(sinks_);
        return true;
    }

} // namespace acr
