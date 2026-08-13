#pragma once

#include <cstdint>
#include <pulse/pulseaudio.h>

namespace HoloNight::System {

// NOLINTBEGIN(readability-identifier-naming,readability-identifier-length,cppcoreguidelines-special-member-functions)
class PulseAudioSystem {
public:
  virtual ~PulseAudioSystem() = default;

  virtual pa_threaded_mainloop *threaded_mainloop_new() = 0;
  virtual pa_mainloop_api *
  threaded_mainloop_get_api(pa_threaded_mainloop *m) = 0;
  virtual int threaded_mainloop_start(pa_threaded_mainloop *m) = 0;
  virtual void threaded_mainloop_stop(pa_threaded_mainloop *m) = 0;
  virtual void threaded_mainloop_free(pa_threaded_mainloop *m) = 0;
  virtual void threaded_mainloop_lock(pa_threaded_mainloop *m) = 0;
  virtual void threaded_mainloop_unlock(pa_threaded_mainloop *m) = 0;

  virtual pa_context *pa_context_new(pa_mainloop_api *api,
                                     const char *name) = 0;
  virtual void pa_context_set_state_callback(pa_context *c,
                                             pa_context_notify_cb_t cb,
                                             void *userdata) = 0;
  virtual void pa_context_set_subscribe_callback(pa_context *c,
                                                 pa_context_subscribe_cb_t cb,
                                                 void *userdata) = 0;
  virtual int pa_context_connect(pa_context *c, const char *server,
                                 pa_context_flags_t flags,
                                 const pa_spawn_api *api) = 0;
  virtual void pa_context_disconnect(pa_context *c) = 0;
  virtual void pa_context_unref(pa_context *c) = 0;

  virtual pa_operation *pa_context_subscribe(pa_context *c,
                                             pa_subscription_mask_t m,
                                             pa_context_success_cb_t cb,
                                             void *userdata) = 0;
  virtual pa_operation *pa_context_get_server_info(pa_context *c,
                                                   pa_server_info_cb_t cb,
                                                   void *userdata) = 0;
  virtual pa_operation *pa_context_get_sink_info_list(pa_context *c,
                                                      pa_sink_info_cb_t cb,
                                                      void *userdata) = 0;
  virtual pa_operation *pa_context_get_source_info_list(pa_context *c,
                                                        pa_source_info_cb_t cb,
                                                        void *userdata) = 0;
  virtual pa_operation *
  pa_context_get_sink_input_info_list(pa_context *c, pa_sink_input_info_cb_t cb,
                                      void *userdata) = 0;
  virtual pa_operation *pa_context_get_source_output_info_list(
      pa_context *c, pa_source_output_info_cb_t cb, void *userdata) = 0;

  virtual pa_operation *pa_context_get_sink_info_by_index(pa_context *c,
                                                          uint32_t idx,
                                                          pa_sink_info_cb_t cb,
                                                          void *userdata) = 0;
  virtual pa_operation *pa_context_get_source_info_by_index(
      pa_context *c, uint32_t idx, pa_source_info_cb_t cb, void *userdata) = 0;
  virtual pa_operation *
  pa_context_get_sink_input_info(pa_context *c, uint32_t idx,
                                 pa_sink_input_info_cb_t cb,
                                 void *userdata) = 0;
  virtual pa_operation *
  pa_context_get_source_output_info(pa_context *c, uint32_t idx,
                                    pa_source_output_info_cb_t cb,
                                    void *userdata) = 0;

  virtual pa_operation *pa_context_set_sink_volume_by_index(
      pa_context *c, uint32_t idx, const pa_cvolume *num,
      pa_context_success_cb_t cb, void *userdata) = 0;
  virtual pa_operation *
  pa_context_set_sink_mute_by_index(pa_context *c, uint32_t idx, int mute,
                                    pa_context_success_cb_t cb,
                                    void *userdata) = 0;
  virtual pa_operation *pa_context_set_source_volume_by_index(
      pa_context *c, uint32_t idx, const pa_cvolume *num,
      pa_context_success_cb_t cb, void *userdata) = 0;
  virtual pa_operation *
  pa_context_set_source_mute_by_index(pa_context *c, uint32_t idx, int mute,
                                      pa_context_success_cb_t cb,
                                      void *userdata) = 0;

  virtual pa_operation *pa_context_set_default_sink(pa_context *c,
                                                    const char *name,
                                                    pa_context_success_cb_t cb,
                                                    void *userdata) = 0;
  virtual pa_operation *
  pa_context_set_default_source(pa_context *c, const char *name,
                                pa_context_success_cb_t cb, void *userdata) = 0;

  virtual pa_operation *pa_context_set_sink_input_volume(
      pa_context *c, uint32_t idx, const pa_cvolume *num,
      pa_context_success_cb_t cb, void *userdata) = 0;
  virtual pa_operation *
  pa_context_set_sink_input_mute(pa_context *c, uint32_t idx, int mute,
                                 pa_context_success_cb_t cb,
                                 void *userdata) = 0;
  virtual pa_operation *pa_context_move_sink_input_by_index(
      pa_context *c, uint32_t idx, uint32_t device_idx,
      pa_context_success_cb_t cb, void *userdata) = 0;

  virtual void pa_operation_unref(pa_operation *o) = 0;
  virtual pa_context_state_t pa_context_get_state(const pa_context *c) = 0;
  virtual int pa_context_errno(const pa_context *c) = 0;

  virtual pa_stream *pa_stream_new(pa_context *c, const char *name,
                                   const pa_sample_spec *ss,
                                   const pa_channel_map *map) = 0;
  virtual void pa_stream_set_state_callback(pa_stream *s,
                                            pa_stream_notify_cb_t cb,
                                            void *userdata) = 0;
  virtual void pa_stream_set_read_callback(pa_stream *s,
                                           pa_stream_request_cb_t cb,
                                           void *userdata) = 0;
  virtual int pa_stream_connect_record(pa_stream *s, const char *dev,
                                       const pa_buffer_attr *attr,
                                       pa_stream_flags_t flags) = 0;
  virtual int pa_stream_peek(pa_stream *s, const void **data,
                             size_t *bytes) = 0;
  virtual int pa_stream_drop(pa_stream *s) = 0;
  virtual int pa_stream_disconnect(pa_stream *s) = 0;
  virtual void pa_stream_unref(pa_stream *s) = 0;
  virtual pa_stream_state_t pa_stream_get_state(const pa_stream *s) = 0;
};

class RealPulseAudioSystem : public PulseAudioSystem {
public:
  pa_threaded_mainloop *threaded_mainloop_new() override {
    return ::pa_threaded_mainloop_new();
  }
  pa_mainloop_api *threaded_mainloop_get_api(pa_threaded_mainloop *m) override {
    return ::pa_threaded_mainloop_get_api(m);
  }
  int threaded_mainloop_start(pa_threaded_mainloop *m) override {
    return ::pa_threaded_mainloop_start(m);
  }
  void threaded_mainloop_stop(pa_threaded_mainloop *m) override {
    ::pa_threaded_mainloop_stop(m);
  }
  void threaded_mainloop_free(pa_threaded_mainloop *m) override {
    ::pa_threaded_mainloop_free(m);
  }
  void threaded_mainloop_lock(pa_threaded_mainloop *m) override {
    ::pa_threaded_mainloop_lock(m);
  }
  void threaded_mainloop_unlock(pa_threaded_mainloop *m) override {
    ::pa_threaded_mainloop_unlock(m);
  }

  pa_context *pa_context_new(pa_mainloop_api *api, const char *name) override {
    return ::pa_context_new(api, name);
  }
  void pa_context_set_state_callback(pa_context *c, pa_context_notify_cb_t cb,
                                     void *userdata) override {
    ::pa_context_set_state_callback(c, cb, userdata);
  }
  void pa_context_set_subscribe_callback(pa_context *c,
                                         pa_context_subscribe_cb_t cb,
                                         void *userdata) override {
    ::pa_context_set_subscribe_callback(c, cb, userdata);
  }
  int pa_context_connect(pa_context *c, const char *server,
                         pa_context_flags_t flags,
                         const pa_spawn_api *api) override {
    return ::pa_context_connect(c, server, flags, api);
  }
  void pa_context_disconnect(pa_context *c) override {
    ::pa_context_disconnect(c);
  }
  void pa_context_unref(pa_context *c) override { ::pa_context_unref(c); }

  pa_operation *pa_context_subscribe(pa_context *c, pa_subscription_mask_t m,
                                     pa_context_success_cb_t cb,
                                     void *userdata) override {
    return ::pa_context_subscribe(c, m, cb, userdata);
  }
  pa_operation *pa_context_get_server_info(pa_context *c,
                                           pa_server_info_cb_t cb,
                                           void *userdata) override {
    return ::pa_context_get_server_info(c, cb, userdata);
  }
  pa_operation *pa_context_get_sink_info_list(pa_context *c,
                                              pa_sink_info_cb_t cb,
                                              void *userdata) override {
    return ::pa_context_get_sink_info_list(c, cb, userdata);
  }
  pa_operation *pa_context_get_source_info_list(pa_context *c,
                                                pa_source_info_cb_t cb,
                                                void *userdata) override {
    return ::pa_context_get_source_info_list(c, cb, userdata);
  }
  pa_operation *pa_context_get_sink_input_info_list(pa_context *c,
                                                    pa_sink_input_info_cb_t cb,
                                                    void *userdata) override {
    return ::pa_context_get_sink_input_info_list(c, cb, userdata);
  }
  pa_operation *pa_context_get_source_output_info_list(
      pa_context *c, pa_source_output_info_cb_t cb, void *userdata) override {
    return ::pa_context_get_source_output_info_list(c, cb, userdata);
  }

  pa_operation *pa_context_get_sink_info_by_index(pa_context *c, uint32_t idx,
                                                  pa_sink_info_cb_t cb,
                                                  void *userdata) override {
    return ::pa_context_get_sink_info_by_index(c, idx, cb, userdata);
  }
  pa_operation *pa_context_get_source_info_by_index(pa_context *c, uint32_t idx,
                                                    pa_source_info_cb_t cb,
                                                    void *userdata) override {
    return ::pa_context_get_source_info_by_index(c, idx, cb, userdata);
  }
  pa_operation *pa_context_get_sink_input_info(pa_context *c, uint32_t idx,
                                               pa_sink_input_info_cb_t cb,
                                               void *userdata) override {
    return ::pa_context_get_sink_input_info(c, idx, cb, userdata);
  }
  pa_operation *pa_context_get_source_output_info(pa_context *c, uint32_t idx,
                                                  pa_source_output_info_cb_t cb,
                                                  void *userdata) override {
    return ::pa_context_get_source_output_info(c, idx, cb, userdata);
  }

  pa_operation *pa_context_set_sink_volume_by_index(pa_context *c, uint32_t idx,
                                                    const pa_cvolume *num,
                                                    pa_context_success_cb_t cb,
                                                    void *userdata) override {
    return ::pa_context_set_sink_volume_by_index(c, idx, num, cb, userdata);
  }
  pa_operation *pa_context_set_sink_mute_by_index(pa_context *c, uint32_t idx,
                                                  int mute,
                                                  pa_context_success_cb_t cb,
                                                  void *userdata) override {
    return ::pa_context_set_sink_mute_by_index(c, idx, mute, cb, userdata);
  }
  pa_operation *pa_context_set_source_volume_by_index(
      pa_context *c, uint32_t idx, const pa_cvolume *num,
      pa_context_success_cb_t cb, void *userdata) override {
    return ::pa_context_set_source_volume_by_index(c, idx, num, cb, userdata);
  }
  pa_operation *pa_context_set_source_mute_by_index(pa_context *c, uint32_t idx,
                                                    int mute,
                                                    pa_context_success_cb_t cb,
                                                    void *userdata) override {
    return ::pa_context_set_source_mute_by_index(c, idx, mute, cb, userdata);
  }

  pa_operation *pa_context_set_default_sink(pa_context *c, const char *name,
                                            pa_context_success_cb_t cb,
                                            void *userdata) override {
    return ::pa_context_set_default_sink(c, name, cb, userdata);
  }
  pa_operation *pa_context_set_default_source(pa_context *c, const char *name,
                                              pa_context_success_cb_t cb,
                                              void *userdata) override {
    return ::pa_context_set_default_source(c, name, cb, userdata);
  }

  pa_operation *pa_context_set_sink_input_volume(pa_context *c, uint32_t idx,
                                                 const pa_cvolume *num,
                                                 pa_context_success_cb_t cb,
                                                 void *userdata) override {
    return ::pa_context_set_sink_input_volume(c, idx, num, cb, userdata);
  }
  pa_operation *pa_context_set_sink_input_mute(pa_context *c, uint32_t idx,
                                               int mute,
                                               pa_context_success_cb_t cb,
                                               void *userdata) override {
    return ::pa_context_set_sink_input_mute(c, idx, mute, cb, userdata);
  }
  pa_operation *pa_context_move_sink_input_by_index(pa_context *c, uint32_t idx,
                                                    uint32_t device_idx,
                                                    pa_context_success_cb_t cb,
                                                    void *userdata) override {
    return ::pa_context_move_sink_input_by_index(c, idx, device_idx, cb,
                                                 userdata);
  }

  void pa_operation_unref(pa_operation *o) override { ::pa_operation_unref(o); }
  pa_context_state_t pa_context_get_state(const pa_context *c) override {
    return ::pa_context_get_state(c);
  }
  int pa_context_errno(const pa_context *c) override {
    return ::pa_context_errno(c);
  }

  pa_stream *pa_stream_new(pa_context *c, const char *name,
                           const pa_sample_spec *ss,
                           const pa_channel_map *map) override {
    return ::pa_stream_new(c, name, ss, map);
  }
  void pa_stream_set_state_callback(pa_stream *s, pa_stream_notify_cb_t cb,
                                    void *userdata) override {
    ::pa_stream_set_state_callback(s, cb, userdata);
  }
  void pa_stream_set_read_callback(pa_stream *s, pa_stream_request_cb_t cb,
                                   void *userdata) override {
    ::pa_stream_set_read_callback(s, cb, userdata);
  }
  int pa_stream_connect_record(pa_stream *s, const char *dev,
                               const pa_buffer_attr *attr,
                               pa_stream_flags_t flags) override {
    return ::pa_stream_connect_record(s, dev, attr, flags);
  }
  int pa_stream_peek(pa_stream *s, const void **data, size_t *bytes) override {
    return ::pa_stream_peek(s, data, bytes);
  }
  int pa_stream_drop(pa_stream *s) override { return ::pa_stream_drop(s); }
  int pa_stream_disconnect(pa_stream *s) override {
    return ::pa_stream_disconnect(s);
  }
  void pa_stream_unref(pa_stream *s) override { ::pa_stream_unref(s); }
  pa_stream_state_t pa_stream_get_state(const pa_stream *s) override {
    return ::pa_stream_get_state(s);
  }
};
// NOLINTEND(readability-identifier-naming,readability-identifier-length,cppcoreguidelines-special-member-functions)

} // namespace HoloNight::System
