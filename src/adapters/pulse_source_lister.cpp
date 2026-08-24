#include "adapters/pulse_source_lister.h"

#include "domain/source_match.h"
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
    struct PulseSourceLister::Impl {
        std::vector<SourceInfo>* sources = nullptr;
        std::string* default_source = nullptr;

        static void server_info_cb(pa_context*, const pa_server_info* info, void* userdata) {
            auto* self = static_cast<Impl*>(userdata);
            if (info && info->default_source_name) {
                *self->default_source = info->default_source_name;
            }
        }

        static void source_info_cb(pa_context*, const pa_source_info* info, int eol, void* userdata) {
            if (eol > 0 || !info) return;
            auto* self = static_cast<Impl*>(userdata);

            SourceInfo s;
            s.index = info->index;
            s.name = safe(info->name);
            s.description = safe(info->description);
            s.is_monitor = info->monitor_of_sink != PA_INVALID_INDEX;
            s.monitor_of_sink_name = safe(info->monitor_of_sink_name);
            self->sources->push_back(std::move(s));
        }
    };

    bool PulseSourceLister::query(std::string& error_message) {
        sources_.clear();
        default_source_.clear();

        QuerySession session;
        if (!session.connect(error_message)) return false;

        Impl sink{&sources_, &default_source_};

        if (!session.wait_operation(pa_context_get_server_info(session.context(), &Impl::server_info_cb, &sink), error_message)) {
            return false;
        }

        if (!session.wait_operation(pa_context_get_source_info_list(session.context(), &Impl::source_info_cb, &sink), error_message)) {
            return false;
        }

        for (auto& s : sources_) {
            s.is_default = (s.name == default_source_);
        }

        sort_sources(sources_);
        return true;
    }

} // namespace acr
