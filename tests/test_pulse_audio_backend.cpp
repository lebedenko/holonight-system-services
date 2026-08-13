#include "PulseAudioBackend.h"

using namespace HoloNight::System;
#include "PulseAudioSystem.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
#include <vector>

// NOLINTBEGIN(readability-named-parameter,readability-identifier-length,cppcoreguidelines-pro-type-reinterpret-cast)
class FakePulseAudioSystem : public PulseAudioSystem {
public:
  pa_threaded_mainloop *mock_mainloop =
      reinterpret_cast<pa_threaded_mainloop *>(0x1111);
  pa_mainloop_api *mock_api = reinterpret_cast<pa_mainloop_api *>(0x2222);
  pa_context *mock_context = reinterpret_cast<pa_context *>(0x3333);
  pa_operation *mock_operation = reinterpret_cast<pa_operation *>(0x4444);

  pa_context_notify_cb_t state_cb = nullptr;
  void *state_userdata = nullptr;
  pa_context_subscribe_cb_t subscribe_cb = nullptr;
  void *subscribe_userdata = nullptr;

  pa_context_state_t context_state = PA_CONTEXT_UNCONNECTED;
  pa_context *context_to_return = mock_context;
  int connect_result = 0;

  // Track function calls
  int start_calls = 0;
  int stop_calls = 0;
  int lock_calls = 0;
  int unlock_calls = 0;
  int connect_calls = 0;
  int disconnect_calls = 0;
  int subscribe_calls = 0;
  int get_server_info_calls = 0;
  int get_sink_list_calls = 0;
  int get_source_list_calls = 0;
  int get_sink_input_list_calls = 0;
  int get_source_output_list_calls = 0;

  // Control action variables
  uint32_t last_sink_idx = 0;
  int last_sink_volume = 0;
  unsigned last_sink_channels = 0;
  bool last_sink_muted = false;
  uint32_t last_source_idx = 0;
  int last_source_volume = 0;
  unsigned last_source_channels = 0;
  bool last_source_muted = false;
  QString last_default_sink;
  QString last_default_source;
  uint32_t last_stream_idx = 0;
  int last_stream_volume = 0;
  unsigned last_stream_channels = 0;
  bool last_stream_muted = false;
  uint32_t last_move_stream_idx = 0;
  uint32_t last_move_device_idx = 0;

  pa_threaded_mainloop *threaded_mainloop_new() override {
    return mock_mainloop;
  }
  pa_mainloop_api *threaded_mainloop_get_api(pa_threaded_mainloop *) override {
    return mock_api;
  }
  int threaded_mainloop_start(pa_threaded_mainloop *) override {
    start_calls++;
    return 0;
  }
  void threaded_mainloop_stop(pa_threaded_mainloop *) override { stop_calls++; }
  void threaded_mainloop_free(pa_threaded_mainloop *) override {}
  void threaded_mainloop_lock(pa_threaded_mainloop *) override { lock_calls++; }
  void threaded_mainloop_unlock(pa_threaded_mainloop *) override {
    unlock_calls++;
  }

  pa_context *pa_context_new(pa_mainloop_api *, const char *) override {
    return context_to_return;
  }
  void pa_context_set_state_callback(pa_context *, pa_context_notify_cb_t cb,
                                     void *userdata) override {
    state_cb = cb;
    state_userdata = userdata;
  }
  void pa_context_set_subscribe_callback(pa_context *,
                                         pa_context_subscribe_cb_t cb,
                                         void *userdata) override {
    subscribe_cb = cb;
    subscribe_userdata = userdata;
  }
  int pa_context_connect(pa_context *, const char *, pa_context_flags_t,
                         const pa_spawn_api *) override {
    connect_calls++;
    return connect_result;
  }
  void pa_context_disconnect(pa_context *) override { disconnect_calls++; }
  void pa_context_unref(pa_context *) override {}

  pa_operation *pa_context_subscribe(pa_context *, pa_subscription_mask_t,
                                     pa_context_success_cb_t, void *) override {
    subscribe_calls++;
    return mock_operation;
  }
  pa_operation *pa_context_get_server_info(pa_context *, pa_server_info_cb_t cb,
                                           void *userdata) override {
    get_server_info_calls++;
    server_info_cb = cb;
    server_info_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_sink_info_list(pa_context *,
                                              pa_sink_info_cb_t cb,
                                              void *userdata) override {
    get_sink_list_calls++;
    sink_cb = cb;
    sink_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_source_info_list(pa_context *,
                                                pa_source_info_cb_t cb,
                                                void *userdata) override {
    get_source_list_calls++;
    source_cb = cb;
    source_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_sink_input_info_list(pa_context *,
                                                    pa_sink_input_info_cb_t cb,
                                                    void *userdata) override {
    get_sink_input_list_calls++;
    sink_input_cb = cb;
    sink_input_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_source_output_info_list(
      pa_context *, pa_source_output_info_cb_t cb, void *userdata) override {
    get_source_output_list_calls++;
    source_output_cb = cb;
    source_output_userdata = userdata;
    return mock_operation;
  }

  // Callbacks capture
  pa_server_info_cb_t server_info_cb = nullptr;
  void *server_info_userdata = nullptr;
  pa_sink_info_cb_t sink_cb = nullptr;
  void *sink_userdata = nullptr;
  pa_source_info_cb_t source_cb = nullptr;
  void *source_userdata = nullptr;
  pa_sink_input_info_cb_t sink_input_cb = nullptr;
  void *sink_input_userdata = nullptr;
  pa_source_output_info_cb_t source_output_cb = nullptr;
  void *source_output_userdata = nullptr;

  pa_sink_info_cb_t sink_by_idx_cb = nullptr;
  void *sink_by_idx_userdata = nullptr;
  pa_source_info_cb_t source_by_idx_cb = nullptr;
  void *source_by_idx_userdata = nullptr;
  pa_sink_input_info_cb_t sink_input_by_idx_cb = nullptr;
  void *sink_input_by_idx_userdata = nullptr;
  pa_source_output_info_cb_t source_output_by_idx_cb = nullptr;
  void *source_output_by_idx_userdata = nullptr;

  pa_operation *pa_context_get_sink_info_by_index(pa_context *, uint32_t idx,
                                                  pa_sink_info_cb_t cb,
                                                  void *userdata) override {
    sink_by_idx_cb = cb;
    sink_by_idx_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_source_info_by_index(pa_context *, uint32_t idx,
                                                    pa_source_info_cb_t cb,
                                                    void *userdata) override {
    source_by_idx_cb = cb;
    source_by_idx_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_sink_input_info(pa_context *, uint32_t idx,
                                               pa_sink_input_info_cb_t cb,
                                               void *userdata) override {
    sink_input_by_idx_cb = cb;
    sink_input_by_idx_userdata = userdata;
    return mock_operation;
  }
  pa_operation *pa_context_get_source_output_info(pa_context *, uint32_t idx,
                                                  pa_source_output_info_cb_t cb,
                                                  void *userdata) override {
    source_output_by_idx_cb = cb;
    source_output_by_idx_userdata = userdata;
    return mock_operation;
  }

  pa_operation *pa_context_set_sink_volume_by_index(pa_context *, uint32_t idx,
                                                    const pa_cvolume *vol,
                                                    pa_context_success_cb_t,
                                                    void *) override {
    last_sink_idx = idx;
    last_sink_channels = vol->channels;
    last_sink_volume = static_cast<int>(
        ((static_cast<uint64_t>(vol->values[0]) * 100) + (PA_VOLUME_NORM / 2)) /
        PA_VOLUME_NORM);
    return mock_operation;
  }
  pa_operation *pa_context_set_sink_mute_by_index(pa_context *, uint32_t idx,
                                                  int mute,
                                                  pa_context_success_cb_t,
                                                  void *) override {
    last_sink_idx = idx;
    last_sink_muted = (mute != 0);
    return mock_operation;
  }
  pa_operation *pa_context_set_source_volume_by_index(pa_context *,
                                                      uint32_t idx,
                                                      const pa_cvolume *vol,
                                                      pa_context_success_cb_t,
                                                      void *) override {
    last_source_idx = idx;
    last_source_channels = vol->channels;
    last_source_volume = static_cast<int>(
        ((static_cast<uint64_t>(vol->values[0]) * 100) + (PA_VOLUME_NORM / 2)) /
        PA_VOLUME_NORM);
    return mock_operation;
  }
  pa_operation *pa_context_set_source_mute_by_index(pa_context *, uint32_t idx,
                                                    int mute,
                                                    pa_context_success_cb_t,
                                                    void *) override {
    last_source_idx = idx;
    last_source_muted = (mute != 0);
    return mock_operation;
  }

  pa_operation *pa_context_set_default_sink(pa_context *, const char *name,
                                            pa_context_success_cb_t,
                                            void *) override {
    last_default_sink = QString::fromUtf8(name);
    return mock_operation;
  }
  pa_operation *pa_context_set_default_source(pa_context *, const char *name,
                                              pa_context_success_cb_t,
                                              void *) override {
    last_default_source = QString::fromUtf8(name);
    return mock_operation;
  }

  pa_operation *pa_context_set_sink_input_volume(pa_context *, uint32_t idx,
                                                 const pa_cvolume *vol,
                                                 pa_context_success_cb_t,
                                                 void *) override {
    last_stream_idx = idx;
    last_stream_channels = vol->channels;
    last_stream_volume = static_cast<int>(
        ((static_cast<uint64_t>(vol->values[0]) * 100) + (PA_VOLUME_NORM / 2)) /
        PA_VOLUME_NORM);
    return mock_operation;
  }
  pa_operation *pa_context_set_sink_input_mute(pa_context *, uint32_t idx,
                                               int mute,
                                               pa_context_success_cb_t,
                                               void *) override {
    last_stream_idx = idx;
    last_stream_muted = (mute != 0);
    return mock_operation;
  }
  pa_operation *pa_context_move_sink_input_by_index(pa_context *, uint32_t idx,
                                                    uint32_t device_idx,
                                                    pa_context_success_cb_t,
                                                    void *) override {
    last_move_stream_idx = idx;
    last_move_device_idx = device_idx;
    return mock_operation;
  }

  void pa_operation_unref(pa_operation *) override {}
  pa_context_state_t pa_context_get_state(const pa_context *) override {
    return context_state;
  }
  int pa_context_errno(const pa_context *) override { return 0; }

  pa_stream *mock_stream = reinterpret_cast<pa_stream *>(0x5555);
  int stream_new_calls = 0;
  pa_sample_spec last_stream_sample_spec{};
  pa_stream_notify_cb_t stream_state_cb = nullptr;
  void *stream_state_userdata = nullptr;
  pa_stream_request_cb_t stream_read_cb = nullptr;
  void *stream_read_userdata = nullptr;
  int stream_connect_record_calls = 0;
  int stream_connect_record_result = 0;
  QString last_stream_connect_record_device;
  pa_stream_flags_t last_stream_connect_record_flags{};
  pa_stream_state_t stream_state = PA_STREAM_UNCONNECTED;
  const void *stream_peek_data = nullptr;
  size_t stream_peek_bytes = 0;
  int stream_peek_result = 0;
  int stream_drop_calls = 0;
  int stream_disconnect_calls = 0;
  int stream_unref_calls = 0;
  std::vector<QString> stream_call_log;

  pa_stream *pa_stream_new(pa_context *, const char *, const pa_sample_spec *ss,
                           const pa_channel_map *) override {
    stream_new_calls++;
    if (ss != nullptr) {
      last_stream_sample_spec = *ss;
    }
    return mock_stream;
  }
  void pa_stream_set_state_callback(pa_stream *, pa_stream_notify_cb_t cb,
                                    void *userdata) override {
    stream_call_log.push_back(cb == nullptr ? QStringLiteral("clear_state_cb")
                                            : QStringLiteral("set_state_cb"));
    stream_state_cb = cb;
    stream_state_userdata = userdata;
  }
  void pa_stream_set_read_callback(pa_stream *, pa_stream_request_cb_t cb,
                                   void *userdata) override {
    stream_call_log.push_back(cb == nullptr ? QStringLiteral("clear_read_cb")
                                            : QStringLiteral("set_read_cb"));
    stream_read_cb = cb;
    stream_read_userdata = userdata;
  }
  int pa_stream_connect_record(pa_stream *, const char *dev,
                               const pa_buffer_attr *,
                               pa_stream_flags_t flags) override {
    stream_connect_record_calls++;
    last_stream_connect_record_device =
        QString::fromUtf8(dev != nullptr ? dev : "");
    last_stream_connect_record_flags = flags;
    return stream_connect_record_result;
  }
  int pa_stream_peek(pa_stream *, const void **data, size_t *bytes) override {
    *data = stream_peek_data;
    *bytes = stream_peek_bytes;
    return stream_peek_result;
  }
  int pa_stream_drop(pa_stream *) override {
    stream_drop_calls++;
    return 0;
  }
  int pa_stream_disconnect(pa_stream *) override {
    stream_call_log.push_back(QStringLiteral("disconnect"));
    stream_disconnect_calls++;
    return 0;
  }
  void pa_stream_unref(pa_stream *) override {
    stream_call_log.push_back(QStringLiteral("unref"));
    stream_unref_calls++;
  }
  pa_stream_state_t pa_stream_get_state(const pa_stream *) override {
    return stream_state;
  }
};
// NOLINTEND(readability-named-parameter,readability-identifier-length,cppcoreguidelines-pro-type-reinterpret-cast)

TEST(PulseAudioBackend, ConstructAndDestroyIsClean) {
  PulseAudioBackend backend;
}

TEST(PulseAudioBackend, StopWithoutStartIsNoop) {
  PulseAudioBackend backend;
  backend.stop();
  backend.stop();
}

TEST(PulseAudioBackend, ControlMethodsAreNopWithoutStart) {
  PulseAudioBackend backend;
  backend.setDeviceVolume(0, 50);
  backend.setDeviceMuted(0, true);
  backend.setSourceVolume(0, 50);
  backend.setSourceMuted(0, true);
  backend.setDefaultOutput(0);
  backend.setDefaultInput(0);
  backend.setStreamVolume(0, 50);
  backend.setStreamMuted(0, false);
  backend.moveStreamToDevice(0, 1);
}

TEST(PulseAudioBackend, StartTransitionToReadyAndQueriesInfo) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy available_spy(&backend, &PulseAudioBackend::availableChanged);

    backend.start();
    EXPECT_EQ(mock_sys.start_calls, 1);
    EXPECT_EQ(mock_sys.connect_calls, 1);
    ASSERT_NE(mock_sys.state_cb, nullptr);

    // Transition to ready
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    QCoreApplication::processEvents();

    // Verify it queried server info and emitted availableChanged
    EXPECT_EQ(mock_sys.subscribe_calls, 1);
    EXPECT_EQ(mock_sys.get_server_info_calls, 1);
    EXPECT_EQ(available_spy.count(), 1);
    EXPECT_TRUE(available_spy.first().at(0).toBool());

    backend.stop();
    EXPECT_EQ(mock_sys.stop_calls, 1);
    EXPECT_EQ(mock_sys.disconnect_calls, 1);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, StartIsIdempotent) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    backend.start();
    backend.start();

    EXPECT_EQ(mock_sys.start_calls, 1);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, StartAndStopCanBeCalledRepeatedly) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    backend.start();
    EXPECT_EQ(mock_sys.start_calls, 1);

    backend.stop();
    EXPECT_EQ(mock_sys.stop_calls, 1);

    backend.start();
    EXPECT_EQ(mock_sys.start_calls, 2);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, HandlesStateFailures) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy available_spy(&backend, &PulseAudioBackend::availableChanged);

    backend.start();
    ASSERT_NE(mock_sys.state_cb, nullptr);

    // Transition to failed
    mock_sys.context_state = PA_CONTEXT_FAILED;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    QCoreApplication::processEvents();

    EXPECT_EQ(available_spy.count(), 1);
    EXPECT_FALSE(available_spy.first().at(0).toBool());
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, ParsesAndEmitsDevicesAndStreams) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    QSignalSpy stream_added_spy(&backend, &PulseAudioBackend::streamAdded);

    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "test-sink";
    info.default_source_name = "test-source";
    ASSERT_NE(mock_sys.server_info_cb, nullptr);
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    EXPECT_EQ(mock_sys.get_sink_list_calls, 1);
    EXPECT_EQ(mock_sys.get_source_list_calls, 1);
    EXPECT_EQ(mock_sys.get_sink_input_list_calls, 1);
    EXPECT_EQ(mock_sys.get_source_output_list_calls, 1);

    // Invoke sink list callback with a test sink
    pa_sink_info sink{};
    sink.index = 5;
    sink.name = "test-sink";
    sink.description = "Test Sink Device";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM / 2; // 50%
    sink.volume.values[1] = PA_VOLUME_NORM / 2;
    sink.mute = 0;

    ASSERT_NE(mock_sys.sink_cb, nullptr);
    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);

    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.id, 5U);
    EXPECT_EQ(dev.name, QStringLiteral("test-sink"));
    EXPECT_EQ(dev.description, QStringLiteral("Test Sink Device"));
    EXPECT_EQ(dev.volume, 50);
    EXPECT_FALSE(dev.muted);
    EXPECT_TRUE(dev.is_default);
    EXPECT_EQ(dev.type, AudioDeviceType::Sink);

    // Invoke sink input callback with a stream
    pa_sink_input_info stream{};
    stream.index = 12;
    stream.name = "Music App";
    stream.sink = 5;
    stream.volume.channels = 2;
    stream.volume.values[0] = PA_VOLUME_NORM; // 100%
    stream.volume.values[1] = PA_VOLUME_NORM;
    stream.mute = 1;
    stream.proplist = pa_proplist_new();
    pa_proplist_sets(stream.proplist, PA_PROP_APPLICATION_NAME, "Spotify");
    pa_proplist_sets(stream.proplist, PA_PROP_APPLICATION_ICON_NAME,
                     "spotify-icon");

    ASSERT_NE(mock_sys.sink_input_cb, nullptr);
    mock_sys.sink_input_cb(mock_sys.mock_context, &stream, 0,
                           mock_sys.sink_input_userdata);

    QCoreApplication::processEvents();

    ASSERT_EQ(stream_added_spy.count(), 1);
    auto ast = stream_added_spy.first().at(0).value<AudioStream>();
    EXPECT_EQ(ast.id, 12U);
    EXPECT_EQ(ast.name, QStringLiteral("Music App"));
    EXPECT_EQ(ast.application, QStringLiteral("Spotify"));
    EXPECT_EQ(ast.icon_name, QStringLiteral("spotify-icon"));
    EXPECT_EQ(ast.volume, 100);
    EXPECT_TRUE(ast.muted);
    EXPECT_EQ(ast.type, AudioStreamType::SinkInput);

    pa_proplist_free(stream.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, ProcessesSubscriptionChangeEvents) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy sink_removed_spy(&backend, &PulseAudioBackend::sinkRemoved);
    QSignalSpy source_removed_spy(&backend, &PulseAudioBackend::sourceRemoved);
    QSignalSpy device_changed_spy(&backend, &PulseAudioBackend::deviceChanged);

    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    ASSERT_NE(mock_sys.subscribe_cb, nullptr);

    // Test SINK REMOVE event
    auto type = static_cast<pa_subscription_event_type_t>(
        PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_REMOVE);
    mock_sys.subscribe_cb(mock_sys.mock_context, type, 42,
                          mock_sys.subscribe_userdata);

    QCoreApplication::processEvents();
    ASSERT_EQ(sink_removed_spy.count(), 1);
    EXPECT_EQ(sink_removed_spy.first().at(0).toUInt(), 42U);
    EXPECT_EQ(source_removed_spy.count(), 0);

    // Test SINK CHANGE event
    type = static_cast<pa_subscription_event_type_t>(
        PA_SUBSCRIPTION_EVENT_SINK | PA_SUBSCRIPTION_EVENT_CHANGE);
    mock_sys.subscribe_cb(mock_sys.mock_context, type, 99,
                          mock_sys.subscribe_userdata);

    ASSERT_NE(mock_sys.sink_by_idx_cb, nullptr);

    pa_sink_info sink{};
    sink.index = 99;
    sink.name = "changed-sink";
    sink.description = "Changed Sink Device";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM * 0.75;
    sink.volume.values[1] = PA_VOLUME_NORM * 0.75;
    sink.mute = 1;

    mock_sys.sink_by_idx_cb(mock_sys.mock_context, &sink, 0,
                            mock_sys.sink_by_idx_userdata);

    QCoreApplication::processEvents();
    ASSERT_EQ(device_changed_spy.count(), 1);
    auto dev = device_changed_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.id, 99U);
    EXPECT_EQ(dev.name, QStringLiteral("changed-sink"));
    EXPECT_TRUE(dev.muted);

    // Test SOURCE REMOVE event — must not cross-contaminate sinkRemoved.
    type = static_cast<
        pa_subscription_event_type_t>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_REMOVE);
    mock_sys.subscribe_cb(mock_sys.mock_context, type, 99,
                          mock_sys.subscribe_userdata);

    QCoreApplication::processEvents();
    ASSERT_EQ(source_removed_spy.count(), 1);
    EXPECT_EQ(source_removed_spy.first().at(0).toUInt(), 99U);
    EXPECT_EQ(sink_removed_spy.count(),
              1); // unchanged from the earlier SINK removal
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, SetControlsTriggersCorrectPulseCalls) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    backend.setDeviceVolume(5, 75);
    ASSERT_NE(mock_sys.sink_by_idx_cb, nullptr);
    pa_sink_info sink{};
    sink.index = 5;
    sink.volume.channels = 6;
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, &sink, 0,
                            mock_sys.sink_by_idx_userdata);
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, nullptr, 1,
                            mock_sys.sink_by_idx_userdata);
    EXPECT_EQ(mock_sys.last_sink_idx, 5U);
    EXPECT_EQ(mock_sys.last_sink_volume, 75);
    EXPECT_EQ(mock_sys.last_sink_channels, 6U);

    backend.setSourceVolume(7, 55);
    ASSERT_NE(mock_sys.source_by_idx_cb, nullptr);
    pa_source_info source{};
    source.index = 7;
    source.volume.channels = 1;
    mock_sys.source_by_idx_cb(mock_sys.mock_context, &source, 0,
                              mock_sys.source_by_idx_userdata);
    mock_sys.source_by_idx_cb(mock_sys.mock_context, nullptr, 1,
                              mock_sys.source_by_idx_userdata);
    EXPECT_EQ(mock_sys.last_source_idx, 7U);
    EXPECT_EQ(mock_sys.last_source_volume, 55);
    EXPECT_EQ(mock_sys.last_source_channels, 1U);

    backend.setDeviceVolume(6, 35);
    ASSERT_NE(mock_sys.sink_by_idx_cb, nullptr);
    pa_sink_info malformed_sink{};
    malformed_sink.index = 6;
    malformed_sink.volume.channels = 0;
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, &malformed_sink, 0,
                            mock_sys.sink_by_idx_userdata);
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, nullptr, 1,
                            mock_sys.sink_by_idx_userdata);
    EXPECT_EQ(mock_sys.last_sink_idx, 6U);
    EXPECT_EQ(mock_sys.last_sink_volume, 35);
    EXPECT_EQ(mock_sys.last_sink_channels, 1U);

    backend.setDeviceMuted(5, true);
    EXPECT_EQ(mock_sys.last_sink_idx, 5U);
    EXPECT_TRUE(mock_sys.last_sink_muted);

    backend.setDefaultOutput(3);
    EXPECT_EQ(mock_sys.last_default_sink, QStringLiteral("3"));

    backend.setDefaultInput(7);
    EXPECT_EQ(mock_sys.last_default_source, QStringLiteral("7"));

    backend.setStreamVolume(15, 90);
    ASSERT_NE(mock_sys.sink_input_by_idx_cb, nullptr);
    pa_sink_input_info stream{};
    stream.index = 15;
    stream.volume.channels = 8;
    mock_sys.sink_input_by_idx_cb(mock_sys.mock_context, &stream, 0,
                                  mock_sys.sink_input_by_idx_userdata);
    mock_sys.sink_input_by_idx_cb(mock_sys.mock_context, nullptr, 1,
                                  mock_sys.sink_input_by_idx_userdata);
    EXPECT_EQ(mock_sys.last_stream_idx, 15U);
    EXPECT_EQ(mock_sys.last_stream_volume, 90);
    EXPECT_EQ(mock_sys.last_stream_channels, 8U);

    backend.moveStreamToDevice(15, 2);
    EXPECT_EQ(mock_sys.last_move_stream_idx, 15U);
    EXPECT_EQ(mock_sys.last_move_device_idx, 2U);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(
    PulseAudioBackend,
    ReconnectsWithExponentialBackoffAndStopsAtCeiling) { // NOLINT(readability-function-cognitive-complexity)
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);
  const std::vector<int> schedule = {10, 20, 40, 80, 160, 300};
  PulseAudioBackend::setReconnectBackoffScheduleForTests(schedule);

  {
    PulseAudioBackend backend;
    QSignalSpy health_spy(&backend, &PulseAudioBackend::healthStateChanged);

    backend.start();
    ASSERT_NE(mock_sys.state_cb, nullptr);

    // kMaxReconnectAttempts is 8 (private, mirrored here as a literal per
    // DESIGN.md/TASKS.md).
    constexpr int kMaxReconnectAttempts = 8;
    // Delay used at each of the 8 scheduled attempts: doubling then held at the
    // schedule's cap.
    const std::vector<int> expected_delays_ms = {10,  20,  40,  80,
                                                 160, 300, 300, 300};
    ASSERT_EQ(expected_delays_ms.size(),
              static_cast<size_t>(kMaxReconnectAttempts));

    mock_sys.context_state = PA_CONTEXT_FAILED;
    int connects_before = mock_sys.connect_calls;

    for (int attempt = 0; attempt < kMaxReconnectAttempts; ++attempt) {
      mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);
      QCoreApplication::processEvents();

      // Reconnect must not fire before the backoff delay elapses.
      EXPECT_EQ(mock_sys.connect_calls, connects_before)
          << "attempt " << attempt;

      QTest::qWait(expected_delays_ms[static_cast<size_t>(attempt)] + 150);
      EXPECT_EQ(mock_sys.connect_calls, connects_before + 1)
          << "attempt " << attempt;
      connects_before = mock_sys.connect_calls;
    }

    // healthStateChanged(Reconnecting) fired once (state stays Reconnecting
    // across retries).
    int reconnecting_count = 0;
    int failed_count = 0;
    for (int i = 0; i < health_spy.count(); ++i) {
      const auto state = health_spy.at(i).at(0).value<AudioHealthState>();
      if (state == AudioHealthState::Reconnecting) {
        reconnecting_count++;
      } else if (state == AudioHealthState::Failed) {
        failed_count++;
      }
    }
    EXPECT_EQ(reconnecting_count, 1);
    EXPECT_EQ(failed_count, 0);

    // The 9th failure (after the 8th scheduled attempt also fails) hits the
    // ceiling: no further reconnect is scheduled, and
    // healthStateChanged(Failed) fires exactly once.
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);
    QCoreApplication::processEvents();
    QTest::qWait(400);
    EXPECT_EQ(mock_sys.connect_calls, connects_before);

    failed_count = 0;
    for (int i = 0; i < health_spy.count(); ++i) {
      if (health_spy.at(i).at(0).value<AudioHealthState>() ==
          AudioHealthState::Failed) {
        failed_count++;
      }
    }
    EXPECT_EQ(failed_count, 1);
  }

  PulseAudioBackend::resetReconnectBackoffSchedule();
  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend,
     ReconnectSucceedsAfterTransientFailureAndPreservesExistingApi) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);
  PulseAudioBackend::setReconnectBackoffScheduleForTests(
      {10, 20, 40, 80, 160, 300});

  {
    PulseAudioBackend backend;
    QSignalSpy health_spy(&backend, &PulseAudioBackend::healthStateChanged);
    QSignalSpy available_spy(&backend, &PulseAudioBackend::availableChanged);

    backend.start();
    ASSERT_NE(mock_sys.state_cb, nullptr);
    const int connects_before = mock_sys.connect_calls;

    // First connect attempt fails.
    mock_sys.context_state = PA_CONTEXT_FAILED;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);
    QCoreApplication::processEvents();

    // Wait for the scheduled reconnect to fire (a fresh context is
    // created/connected).
    QTest::qWait(160);
    EXPECT_EQ(mock_sys.connect_calls, connects_before + 1);

    // The second attempt succeeds.
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);
    QCoreApplication::processEvents();

    ASSERT_GE(health_spy.count(), 2);
    EXPECT_EQ(health_spy.first().at(0).value<AudioHealthState>(),
              AudioHealthState::Reconnecting);
    EXPECT_EQ(health_spy.last().at(0).value<AudioHealthState>(),
              AudioHealthState::Connected);

    ASSERT_GE(available_spy.count(), 2);
    EXPECT_FALSE(available_spy.at(available_spy.count() - 2).at(0).toBool());
    EXPECT_TRUE(available_spy.last().at(0).toBool());

    // Existing control-plane calls still work identically after recovery — no
    // stale state left behind by the reconnect cycle (REQ-NF-001: existing
    // audio-API signatures unchanged).
    backend.setDeviceVolume(5, 60);
    ASSERT_NE(mock_sys.sink_by_idx_cb, nullptr);
    pa_sink_info sink{};
    sink.index = 5;
    sink.volume.channels = 2;
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, &sink, 0,
                            mock_sys.sink_by_idx_userdata);
    mock_sys.sink_by_idx_cb(mock_sys.mock_context, nullptr, 1,
                            mock_sys.sink_by_idx_userdata);
    EXPECT_EQ(mock_sys.last_sink_idx, 5U);
    EXPECT_EQ(mock_sys.last_sink_volume, 60);
  }

  PulseAudioBackend::resetReconnectBackoffSchedule();
  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, ImmediateConnectFailuresRetryAndStopMainloopSafely) {
  FakePulseAudioSystem mock_sys;
  mock_sys.context_to_return = nullptr;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);
  PulseAudioBackend::setReconnectBackoffScheduleForTests({1});

  {
    PulseAudioBackend backend;
    QSignalSpy health_spy(&backend, &PulseAudioBackend::healthStateChanged);
    backend.start();

    QTRY_VERIFY_WITH_TIMEOUT(health_spy.count() >= 2, 1000);
    EXPECT_EQ(health_spy.first().at(0).value<AudioHealthState>(),
              AudioHealthState::Reconnecting);
    EXPECT_EQ(health_spy.last().at(0).value<AudioHealthState>(),
              AudioHealthState::Failed);
    EXPECT_EQ(mock_sys.connect_calls, 0);
  }

  EXPECT_EQ(mock_sys.stop_calls, 1);
  PulseAudioBackend::resetReconnectBackoffSchedule();
  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, SynchronousConnectFailuresReachRetryCeiling) {
  FakePulseAudioSystem mock_sys;
  mock_sys.connect_result = -1;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);
  PulseAudioBackend::setReconnectBackoffScheduleForTests({1});

  {
    PulseAudioBackend backend;
    QSignalSpy health_spy(&backend, &PulseAudioBackend::healthStateChanged);
    backend.start();

    QTRY_VERIFY_WITH_TIMEOUT(health_spy.count() >= 2, 1000);
    EXPECT_EQ(health_spy.last().at(0).value<AudioHealthState>(),
              AudioHealthState::Failed);
    EXPECT_EQ(mock_sys.connect_calls,
              9); // Initial attempt plus the bounded eight retries.
  }

  PulseAudioBackend::resetReconnectBackoffSchedule();
  PulseAudioBackend::resetPulseAudioSystem();
}

// ============================================================
// T-009: classifyBusType() and metadata extraction
// ============================================================

TEST(ClassifyBusType, HdmiFormFactorIsDigital) {
  pa_proplist *proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_DEVICE_FORM_FACTOR, "hdmi");
  EXPECT_EQ(
      classifyBusType(proplist, QStringLiteral("alsa_output.pci"), QString()),
      QStringLiteral("Digital"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, DeviceNameContainingSpdifIsDigital) {
  pa_proplist *proplist = pa_proplist_new();
  EXPECT_EQ(classifyBusType(proplist,
                            QStringLiteral("alsa_output.spdif-surround"),
                            QString()),
            QStringLiteral("Digital"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, BluetoothBusIsBluetooth) {
  pa_proplist *proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_DEVICE_BUS, "bluetooth");
  EXPECT_EQ(classifyBusType(proplist, QStringLiteral("bluez_sink"), QString()),
            QStringLiteral("Bluetooth"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, UsbBusIsDigital) {
  pa_proplist *proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_DEVICE_BUS, "usb");
  EXPECT_EQ(classifyBusType(proplist, QStringLiteral("usb_headset"), QString()),
            QStringLiteral("Digital"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, PciBusIsAnalog) {
  pa_proplist *proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_DEVICE_BUS, "pci");
  EXPECT_EQ(
      classifyBusType(proplist, QStringLiteral("alsa_output.pci"), QString()),
      QStringLiteral("Analog"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, EmptyProplistIsUnknown) {
  pa_proplist *proplist = pa_proplist_new();
  EXPECT_EQ(classifyBusType(proplist, QString(), QString()),
            QStringLiteral("Unknown"));
  pa_proplist_free(proplist);
}

TEST(ClassifyBusType, NullProplistIsUnknown) {
  EXPECT_EQ(classifyBusType(nullptr, QString(), QString()),
            QStringLiteral("Unknown"));
}

TEST(PulseAudioBackend,
     SinkToDevicePopulatesChannelCountSampleRateAndMetadata) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "test-sink";
    info.default_source_name = "test-source";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 7;
    sink.name = "test-sink";
    sink.description = "Test Sink";
    sink.channel_map.channels = 2;
    sink.sample_spec.rate = 48000;
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_BUS, "pci");
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_ICON_NAME, "audio-card");

    ASSERT_NE(mock_sys.sink_cb, nullptr);
    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.channel_count, 2);
    EXPECT_EQ(dev.sample_rate, 48000U);
    EXPECT_EQ(dev.bus_type, QStringLiteral("Analog"));
    EXPECT_EQ(dev.icon_name, QStringLiteral("audio-card"));
    EXPECT_TRUE(dev.codec.isEmpty());

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, HdmiSinkOverridesGenericCardIconWithDisplayIcon) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info server_info{};
    server_info.default_sink_name = "alsa_output.pci.hdmi-stereo";
    mock_sys.server_info_cb(mock_sys.mock_context, &server_info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 8;
    sink.name = "alsa_output.pci.hdmi-stereo";
    sink.description = "Digital Stereo (HDMI)";
    sink.channel_map.channels = 2;
    sink.sample_spec.rate = 48000;
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_ICON_NAME, "audio-card");

    ASSERT_NE(mock_sys.sink_cb, nullptr);
    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    const auto device = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(device.bus_type, QStringLiteral("Digital"));
    EXPECT_EQ(device.icon_name, QStringLiteral("video-display"));

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, SourceToDevicePopulatesChannelCountAndSampleRate) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "test-sink";
    info.default_source_name = "test-source";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_source_info source{};
    source.index = 9;
    source.name = "test-source";
    source.description = "Test Source";
    source.monitor_of_sink = PA_INVALID_INDEX;
    source.channel_map.channels = 1;
    source.sample_spec.rate = 44100;
    source.volume.channels = 1;
    source.volume.values[0] = PA_VOLUME_NORM;
    source.proplist = pa_proplist_new();
    pa_proplist_sets(source.proplist, PA_PROP_DEVICE_FORM_FACTOR, "microphone");

    ASSERT_NE(mock_sys.source_cb, nullptr);
    mock_sys.source_cb(mock_sys.mock_context, &source, 0,
                       mock_sys.source_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.channel_count, 1);
    EXPECT_EQ(dev.sample_rate, 44100U);
    EXPECT_EQ(dev.icon_name, QStringLiteral("audio-input-microphone"));

    pa_proplist_free(source.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, GenericAnalogCardSourceUsesMicrophoneIcon) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "";
    info.default_source_name = "alsa_input.pci-0000_00_1f.3.analog-stereo";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_source_info source{};
    source.index = 10;
    source.name = info.default_source_name;
    source.description = "Built-in Audio Analog Stereo";
    source.monitor_of_sink = PA_INVALID_INDEX;
    source.channel_map.channels = 2;
    source.sample_spec.rate = 48000;
    source.volume.channels = 2;
    source.volume.values[0] = PA_VOLUME_NORM;
    source.volume.values[1] = PA_VOLUME_NORM;
    source.proplist = pa_proplist_new();
    pa_proplist_sets(source.proplist, PA_PROP_DEVICE_ICON_NAME,
                     "audio-card-analog");

    ASSERT_NE(mock_sys.source_cb, nullptr);
    mock_sys.source_cb(mock_sys.mock_context, &source, 0,
                       mock_sys.source_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    const auto device =
        device_added_spy.constFirst().constFirst().value<AudioDevice>();
    EXPECT_EQ(device.icon_name, QStringLiteral("audio-input-microphone"));

    pa_proplist_free(source.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, BluetoothCodecPrefersBluezCodecName) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "bt-sink";
    info.default_source_name = "";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 11;
    sink.name = "bt-sink";
    sink.description = "Bluetooth Headphones";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_BUS, "bluetooth");
    pa_proplist_sets(sink.proplist, "bluez.codec_name", "AAC");
    pa_proplist_sets(sink.proplist, "bluetooth.codec", "SBC");

    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.bus_type, QStringLiteral("Bluetooth"));
    EXPECT_EQ(dev.codec, QStringLiteral("AAC"));

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, BluetoothCodecFallsBackToBluetoothCodecKey) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "bt-sink";
    info.default_source_name = "";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 12;
    sink.name = "bt-sink";
    sink.description = "Bluetooth Headphones";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_BUS, "bluetooth");
    pa_proplist_sets(sink.proplist, "bluetooth.codec", "SBC");

    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.codec, QStringLiteral("SBC"));

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, BluetoothCodecDefaultsToPcmWhenBothKeysAbsent) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "bt-sink";
    info.default_source_name = "";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 13;
    sink.name = "bt-sink";
    sink.description = "Bluetooth Headphones";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_BUS, "bluetooth");

    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.codec, QStringLiteral("PCM"));

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, NonBluetoothDeviceCodecStaysEmpty) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy device_added_spy(&backend, &PulseAudioBackend::deviceAdded);
    backend.start();
    mock_sys.context_state = PA_CONTEXT_READY;
    mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

    pa_server_info info{};
    info.default_sink_name = "usb-sink";
    info.default_source_name = "";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    pa_sink_info sink{};
    sink.index = 14;
    sink.name = "usb-sink";
    sink.description = "USB DAC";
    sink.volume.channels = 2;
    sink.volume.values[0] = PA_VOLUME_NORM;
    sink.volume.values[1] = PA_VOLUME_NORM;
    sink.proplist = pa_proplist_new();
    pa_proplist_sets(sink.proplist, PA_PROP_DEVICE_BUS, "usb");
    pa_proplist_sets(sink.proplist, "bluez.codec_name",
                     "AAC"); // present but irrelevant — not Bluetooth

    mock_sys.sink_cb(mock_sys.mock_context, &sink, 0, mock_sys.sink_userdata);
    QCoreApplication::processEvents();

    ASSERT_EQ(device_added_spy.count(), 1);
    auto dev = device_added_spy.first().at(0).value<AudioDevice>();
    EXPECT_EQ(dev.bus_type, QStringLiteral("Digital"));
    EXPECT_TRUE(dev.codec.isEmpty());

    pa_proplist_free(sink.proplist);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

// ============================================================
// T-010: input-level monitoring stream lifecycle
// ============================================================

namespace {

void bringToReadyWithDefaultSource(PulseAudioBackend &backend,
                                   FakePulseAudioSystem &mock_sys,
                                   const char *default_source_name) {
  backend.start();
  mock_sys.context_state = PA_CONTEXT_READY;
  mock_sys.state_cb(mock_sys.mock_context, mock_sys.state_userdata);

  pa_server_info info{};
  info.default_sink_name = "test-sink";
  info.default_source_name = default_source_name;
  mock_sys.server_info_cb(mock_sys.mock_context, &info,
                          mock_sys.server_info_userdata);
}

} // namespace

TEST(PulseAudioBackend,
     StartInputLevelMonitorConnectsRecordStreamToDefaultSource) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");

    backend.startInputLevelMonitor();

    EXPECT_EQ(mock_sys.stream_new_calls, 1);
    EXPECT_EQ(mock_sys.last_stream_sample_spec.rate, 30U);
    EXPECT_EQ(mock_sys.last_stream_sample_spec.channels, 1);
    EXPECT_EQ(mock_sys.last_stream_sample_spec.format, PA_SAMPLE_FLOAT32LE);
    EXPECT_EQ(mock_sys.stream_connect_record_calls, 1);
    EXPECT_EQ(mock_sys.last_stream_connect_record_device,
              QStringLiteral("mic-a"));
    EXPECT_TRUE((mock_sys.last_stream_connect_record_flags &
                 PA_STREAM_PEAK_DETECT) != 0);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, StartInputLevelMonitorIsIdempotent) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");

    backend.startInputLevelMonitor();
    backend.startInputLevelMonitor();

    EXPECT_EQ(mock_sys.stream_new_calls, 1);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend,
     FailedInputLevelConnectionCleansUpAndRetriesAreBounded) {
  FakePulseAudioSystem mock_sys;
  mock_sys.stream_connect_record_result = -1;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");

    backend.startInputLevelMonitor();
    for (int attempt = 0; attempt < 5; ++attempt) {
      QCoreApplication::processEvents();
    }

    EXPECT_EQ(mock_sys.stream_connect_record_calls, 4);
    EXPECT_EQ(mock_sys.stream_unref_calls, 4);
    EXPECT_EQ(mock_sys.stream_disconnect_calls, 0);
    EXPECT_EQ(mock_sys.stream_state_cb, nullptr);
    EXPECT_EQ(mock_sys.stream_read_cb, nullptr);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, ReadCallbackEmitsScaledInputLevel) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy level_spy(&backend, &PulseAudioBackend::inputLevelChanged);
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");
    backend.startInputLevelMonitor();

    const float sample = 0.5F;
    mock_sys.stream_peek_data = &sample;
    mock_sys.stream_peek_bytes = sizeof(sample);
    mock_sys.stream_peek_result = 0;

    ASSERT_NE(mock_sys.stream_read_cb, nullptr);
    mock_sys.stream_read_cb(mock_sys.mock_stream, sizeof(sample),
                            mock_sys.stream_read_userdata);
    QCoreApplication::processEvents();

    ASSERT_GE(level_spy.count(), 1);
    EXPECT_EQ(level_spy.last().at(0).toInt(), 50);
    EXPECT_EQ(mock_sys.stream_drop_calls, 1);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, StreamFailedStateEmitsZeroLevel) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy level_spy(&backend, &PulseAudioBackend::inputLevelChanged);
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");
    backend.startInputLevelMonitor();

    mock_sys.stream_state = PA_STREAM_FAILED;
    ASSERT_NE(mock_sys.stream_state_cb, nullptr);
    mock_sys.stream_state_cb(mock_sys.mock_stream,
                             mock_sys.stream_state_userdata);
    QCoreApplication::processEvents();

    ASSERT_GE(level_spy.count(), 1);
    EXPECT_EQ(level_spy.last().at(0).toInt(), 0);
    EXPECT_EQ(mock_sys.stream_disconnect_calls, 1);
    EXPECT_EQ(mock_sys.stream_unref_calls, 1);
    EXPECT_EQ(mock_sys.stream_new_calls, 2);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend,
     StopInputLevelMonitorClearsCallbacksBeforeDisconnectAndUnref) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    QSignalSpy level_spy(&backend, &PulseAudioBackend::inputLevelChanged);
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");
    backend.startInputLevelMonitor();

    backend.stopInputLevelMonitor();
    QCoreApplication::processEvents();

    ASSERT_GE(mock_sys.stream_call_log.size(), 4U);
    const auto tail_start = mock_sys.stream_call_log.size() - 4;
    EXPECT_EQ(mock_sys.stream_call_log[tail_start],
              QStringLiteral("clear_state_cb"));
    EXPECT_EQ(mock_sys.stream_call_log[tail_start + 1],
              QStringLiteral("clear_read_cb"));
    EXPECT_EQ(mock_sys.stream_call_log[tail_start + 2],
              QStringLiteral("disconnect"));
    EXPECT_EQ(mock_sys.stream_call_log[tail_start + 3],
              QStringLiteral("unref"));
    EXPECT_EQ(mock_sys.stream_disconnect_calls, 1);
    EXPECT_EQ(mock_sys.stream_unref_calls, 1);
    ASSERT_GE(level_spy.count(), 1);
    EXPECT_EQ(level_spy.last().at(0).toInt(), 0);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, StopInputLevelMonitorIsIdempotent) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");
    backend.startInputLevelMonitor();

    backend.stopInputLevelMonitor();
    backend.stopInputLevelMonitor();

    EXPECT_EQ(mock_sys.stream_disconnect_calls, 1);
    EXPECT_EQ(mock_sys.stream_unref_calls, 1);
  }

  PulseAudioBackend::resetPulseAudioSystem();
}

TEST(PulseAudioBackend, DefaultSourceChangeWhileMonitoringReconnectsStream) {
  FakePulseAudioSystem mock_sys;
  PulseAudioBackend::setPulseAudioSystem(&mock_sys);

  {
    PulseAudioBackend backend;
    bringToReadyWithDefaultSource(backend, mock_sys, "mic-a");
    backend.startInputLevelMonitor();
    ASSERT_EQ(mock_sys.stream_new_calls, 1);

    pa_server_info info{};
    info.default_sink_name = "test-sink";
    info.default_source_name = "mic-b";
    mock_sys.server_info_cb(mock_sys.mock_context, &info,
                            mock_sys.server_info_userdata);

    EXPECT_EQ(mock_sys.stream_new_calls, 2);
    EXPECT_EQ(mock_sys.last_stream_connect_record_device,
              QStringLiteral("mic-b"));
  }

  PulseAudioBackend::resetPulseAudioSystem();
}
