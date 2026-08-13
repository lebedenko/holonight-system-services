#include "PulseAudioBackend.h"

#include "PulseAudioSystem.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <pulse/pulseaudio.h>
#include <tuple>

namespace HoloNight::System {

Q_LOGGING_CATEGORY(lcAudioBackend, "holonight.audio.backend")

static constexpr uint32_t kVolumeNorm = PA_VOLUME_NORM;

static int clampPercent(int percent) { return std::clamp(percent, 0, 100); }

static uint32_t volumeFromPercent(int percent) {
  const int clamped = clampPercent(percent);
  return static_cast<uint32_t>(
      ((static_cast<uint64_t>(clamped) * kVolumeNorm) + 50) / 100);
}

static uint8_t volumeToPercent(uint32_t raw) {
  const auto pct = static_cast<int>(
      ((static_cast<uint64_t>(raw) * 100) + (kVolumeNorm / 2)) / kVolumeNorm);
  return static_cast<uint8_t>(clampPercent(pct));
}

static PulseAudioSystem *s_pa_system =
    nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static RealPulseAudioSystem
    s_real_pa_system; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static const std::vector<int> kDefaultReconnectBackoffMs = {1000, 2000,  4000,
                                                            8000, 16000, 30000};
static std::vector<int>
    s_reconnect_backoff_ms = // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    kDefaultReconnectBackoffMs;

static PulseAudioSystem *pulseSystem() {
  return s_pa_system != nullptr ? s_pa_system : &s_real_pa_system;
}

static bool looksLikeDigitalOutput(const QString &form_factor,
                                   const QString &device_name,
                                   const QString &active_port_name) {
  return form_factor == QStringLiteral("hdmi") ||
         device_name.contains(QStringLiteral("hdmi"), Qt::CaseInsensitive) ||
         device_name.contains(QStringLiteral("spdif"), Qt::CaseInsensitive) ||
         device_name.contains(QStringLiteral("iec958"), Qt::CaseInsensitive) ||
         active_port_name.contains(QStringLiteral("hdmi"),
                                   Qt::CaseInsensitive) ||
         active_port_name.contains(QStringLiteral("spdif"),
                                   Qt::CaseInsensitive) ||
         active_port_name.contains(QStringLiteral("iec958"),
                                   Qt::CaseInsensitive);
}

QString classifyBusType(const pa_proplist *proplist, const QString &device_name,
                        const QString &active_port_name) {
  const char *form_factor =
      proplist != nullptr
          ? pa_proplist_gets(proplist, PA_PROP_DEVICE_FORM_FACTOR)
          : nullptr;
  const QString form_factor_str =
      QString::fromUtf8(form_factor != nullptr ? form_factor : "");
  if (looksLikeDigitalOutput(form_factor_str, device_name, active_port_name)) {
    return QStringLiteral("Digital");
  }

  const char *bus = proplist != nullptr
                        ? pa_proplist_gets(proplist, PA_PROP_DEVICE_BUS)
                        : nullptr;
  const QString bus_str = QString::fromUtf8(bus != nullptr ? bus : "");
  if (bus_str == QStringLiteral("bluetooth")) {
    return QStringLiteral("Bluetooth");
  }
  if (bus_str == QStringLiteral("usb")) {
    return QStringLiteral("Digital");
  }
  if (!bus_str.isEmpty()) {
    return QStringLiteral("Analog");
  }
  return QStringLiteral("Unknown");
}

static QString iconNameFromProplist(const pa_proplist *proplist, bool is_sink,
                                    const QString &device_name,
                                    const QString &active_port_name) {
  const char *icon = proplist != nullptr
                         ? pa_proplist_gets(proplist, PA_PROP_DEVICE_ICON_NAME)
                         : nullptr;
  const QString reported_icon = QString::fromUtf8(icon != nullptr ? icon : "");

  const char *form_factor =
      proplist != nullptr
          ? pa_proplist_gets(proplist, PA_PROP_DEVICE_FORM_FACTOR)
          : nullptr;
  const QString form_factor_str =
      QString::fromUtf8(form_factor != nullptr ? form_factor : "");
  const bool generic_card_icon =
      reported_icon.isEmpty() ||
      reported_icon == QStringLiteral("audio-card") ||
      reported_icon.startsWith(QStringLiteral("audio-card-"));
  if (is_sink && generic_card_icon &&
      looksLikeDigitalOutput(form_factor_str, device_name, active_port_name)) {
    return QStringLiteral("video-display");
  }
  if (!is_sink && generic_card_icon) {
    return QStringLiteral("audio-input-microphone");
  }
  if (!reported_icon.isEmpty()) {
    return reported_icon;
  }
  if (form_factor_str == QStringLiteral("headphone")) {
    return QStringLiteral("audio-headphones");
  }
  if (form_factor_str == QStringLiteral("headset") ||
      form_factor_str == QStringLiteral("hands-free")) {
    return QStringLiteral("audio-headset");
  }
  if (form_factor_str == QStringLiteral("microphone")) {
    return QStringLiteral("audio-input-microphone");
  }
  if (form_factor_str == QStringLiteral("speaker") ||
      form_factor_str == QStringLiteral("internal") ||
      form_factor_str.isEmpty()) {
    return is_sink ? QStringLiteral("audio-speakers")
                   : QStringLiteral("audio-input-microphone");
  }
  return {};
}

static QString bluetoothCodecFromProplist(const pa_proplist *proplist) {
  if (proplist == nullptr) {
    return QStringLiteral("PCM");
  }
  const char *codec = pa_proplist_gets(proplist, "bluez.codec_name");
  if (codec != nullptr && *codec != '\0') {
    return QString::fromUtf8(codec);
  }
  codec = pa_proplist_gets(proplist, "bluetooth.codec");
  if (codec != nullptr && *codec != '\0') {
    return QString::fromUtf8(codec);
  }
  return QStringLiteral("PCM");
}

void PulseAudioBackend::setPulseAudioSystem(PulseAudioSystem *sys) {
  s_pa_system = sys;
}

void PulseAudioBackend::resetPulseAudioSystem() { s_pa_system = nullptr; }

void PulseAudioBackend::setReconnectBackoffScheduleForTests(
    std::vector<int> delays_ms) {
  s_reconnect_backoff_ms = std::move(delays_ms);
}

void PulseAudioBackend::resetReconnectBackoffSchedule() {
  s_reconnect_backoff_ms = kDefaultReconnectBackoffMs;
}

// ============================================================
struct PulseAudioBackend::Impl {
  struct VolumeRequest {
    int percent;
  };

  PulseAudioBackend *backend{nullptr};
  bool started{false};
  bool mainloop_running{false};
  pa_threaded_mainloop *mainloop{nullptr};
  pa_mainloop_api *api{nullptr};
  pa_context *context{nullptr};
  QString default_sink_name;
  QString default_source_name;
  mutable QSet<quint64> metadata_warned_ids;
  pa_stream *level_stream{nullptr};
  bool level_stream_warned{false};
  bool level_monitor_requested{false};
  int level_stream_retry_count{0};

  [[nodiscard]] AudioDevice sinkToDevice(const pa_sink_info *info) const;
  [[nodiscard]] AudioDevice sourceToDevice(const pa_source_info *info) const;
  void warnIfMetadataMissing(const AudioDevice &dev) const;

  [[nodiscard]] bool startLevelStreamLocked();
  void stopLevelStreamLocked();
  [[nodiscard]] static AudioStream
  sinkInputToStream(const pa_sink_input_info *info);
  [[nodiscard]] static AudioStream
  sourceOutputToStream(const pa_source_output_info *info);

  void onContextReady();
  void teardownContext();
  [[nodiscard]] bool connectNewContext();
  void queryServerInfo();
  void querySinks();
  void querySources();
  void querySinkInputs();
  void querySourceOutputs();

  void handleSinkEvent(pa_context *ctx, int evt, uint32_t idx);
  void handleSourceEvent(pa_context *ctx, int evt, uint32_t idx);
  void handleSinkInputEvent(pa_context *ctx, int evt, uint32_t idx);
  void handleSourceOutputEvent(pa_context *ctx, int evt, uint32_t idx);

  static void contextStateCallback(pa_context *ctx, void *userdata);
  static void subscribeCallback(pa_context *ctx,
                                pa_subscription_event_type_t type, uint32_t idx,
                                void *userdata);
  static void serverInfoCallback(pa_context *ctx, const pa_server_info *info,
                                 void *userdata);
  static void sinkListCallback(pa_context *ctx, const pa_sink_info *info,
                               int eol, void *userdata);
  static void sourceListCallback(pa_context *ctx, const pa_source_info *info,
                                 int eol, void *userdata);
  static void sinkInputListCallback(pa_context *ctx,
                                    const pa_sink_input_info *info, int eol,
                                    void *userdata);
  static void sourceOutputListCallback(pa_context *ctx,
                                       const pa_source_output_info *info,
                                       int eol, void *userdata);
  static void sinkChangedCallback(pa_context *ctx, const pa_sink_info *info,
                                  int eol, void *userdata);
  static void sourceChangedCallback(pa_context *ctx, const pa_source_info *info,
                                    int eol, void *userdata);
  static void sinkInputChangedCallback(pa_context *ctx,
                                       const pa_sink_input_info *info, int eol,
                                       void *userdata);
  static void sourceOutputChangedCallback(pa_context *ctx,
                                          const pa_source_output_info *info,
                                          int eol, void *userdata);
  static void setSinkVolumeCallback(pa_context *ctx, const pa_sink_info *info,
                                    int eol, void *userdata);
  static void setSourceVolumeCallback(pa_context *ctx,
                                      const pa_source_info *info, int eol,
                                      void *userdata);
  static void setSinkInputVolumeCallback(pa_context *ctx,
                                         const pa_sink_input_info *info,
                                         int eol, void *userdata);

  static void levelStreamStateCallback(pa_stream *stream, void *userdata);
  static void levelStreamReadCallback(pa_stream *stream, size_t length,
                                      void *userdata);
};

// ============================================================
// Conversion helpers
// ============================================================

AudioDevice
PulseAudioBackend::Impl::sinkToDevice(const pa_sink_info *info) const {
  AudioDevice dev;
  dev.id = info->index;
  dev.name = QString::fromUtf8(info->name != nullptr ? info->name : "");
  dev.description =
      QString::fromUtf8(info->description != nullptr ? info->description : "");
  dev.volume = volumeToPercent(pa_cvolume_avg(&info->volume));
  dev.muted = info->mute != 0;
  dev.is_default = (dev.name == default_sink_name);
  dev.type = AudioDeviceType::Sink;
  dev.channel_count = static_cast<uint8_t>(info->channel_map.channels);
  dev.sample_rate = info->sample_spec.rate;
  const QString active_port_name =
      (info->active_port != nullptr)
          ? QString::fromUtf8(info->active_port->name != nullptr
                                  ? info->active_port->name
                                  : "")
          : QString();
  dev.bus_type = classifyBusType(info->proplist, dev.name, active_port_name);
  dev.icon_name = iconNameFromProplist(info->proplist, /*is_sink=*/true,
                                       dev.name, active_port_name);
  if (dev.bus_type == QStringLiteral("Bluetooth")) {
    dev.codec = bluetoothCodecFromProplist(info->proplist);
  }
  warnIfMetadataMissing(dev);
  return dev;
}

AudioDevice
PulseAudioBackend::Impl::sourceToDevice(const pa_source_info *info) const {
  AudioDevice dev;
  dev.id = info->index;
  dev.name = QString::fromUtf8(info->name != nullptr ? info->name : "");
  dev.description =
      QString::fromUtf8(info->description != nullptr ? info->description : "");
  dev.volume = volumeToPercent(pa_cvolume_avg(&info->volume));
  dev.muted = info->mute != 0;
  dev.is_default = (dev.name == default_source_name);
  dev.type = AudioDeviceType::Source;
  dev.channel_count = static_cast<uint8_t>(info->channel_map.channels);
  dev.sample_rate = info->sample_spec.rate;
  const QString active_port_name =
      (info->active_port != nullptr)
          ? QString::fromUtf8(info->active_port->name != nullptr
                                  ? info->active_port->name
                                  : "")
          : QString();
  dev.bus_type = classifyBusType(info->proplist, dev.name, active_port_name);
  dev.icon_name = iconNameFromProplist(info->proplist, /*is_sink=*/false,
                                       dev.name, active_port_name);
  if (dev.bus_type == QStringLiteral("Bluetooth")) {
    dev.codec = bluetoothCodecFromProplist(info->proplist);
  }
  warnIfMetadataMissing(dev);
  return dev;
}

void PulseAudioBackend::Impl::warnIfMetadataMissing(
    const AudioDevice &dev) const {
  const bool metadata_missing =
      dev.bus_type == QStringLiteral("Unknown") || dev.icon_name.isEmpty();
  const quint64 warning_key = (static_cast<quint64>(dev.type) << 32U) | dev.id;
  if (!metadata_missing || metadata_warned_ids.contains(warning_key)) {
    return;
  }
  metadata_warned_ids.insert(warning_key);
  qCWarning(lcAudioBackend)
      << "PulseAudioBackend: incomplete metadata for device id" << dev.id
      << "name" << dev.name;
}

// Assumes the threaded mainloop lock is already held by the caller.
bool PulseAudioBackend::Impl::startLevelStreamLocked() {
  if (context == nullptr || level_stream != nullptr) {
    return level_stream != nullptr;
  }

  pa_sample_spec sample_spec{};
  sample_spec.format = PA_SAMPLE_FLOAT32LE;
  sample_spec.rate = 30;
  sample_spec.channels = 1;

  pa_stream *stream = pulseSystem()->pa_stream_new(
      context, "holonight-shell-input-level", &sample_spec, nullptr);
  if (stream == nullptr) {
    return false;
  }

  pulseSystem()->pa_stream_set_state_callback(stream, levelStreamStateCallback,
                                              this);
  pulseSystem()->pa_stream_set_read_callback(stream, levelStreamReadCallback,
                                             this);

  pa_buffer_attr attr{};
  attr.maxlength = UINT32_MAX;
  attr.fragsize = pa_frame_size(&sample_spec);

  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) — OR-ing valid
  // bitmask constants
  const auto flags = static_cast<pa_stream_flags_t>(
      PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY |
      PA_STREAM_AUTO_TIMING_UPDATE);
  const int connect_result = pulseSystem()->pa_stream_connect_record(
      stream, default_source_name.toUtf8().constData(), &attr, flags);
  if (connect_result < 0) {
    pulseSystem()->pa_stream_set_state_callback(stream, nullptr, nullptr);
    pulseSystem()->pa_stream_set_read_callback(stream, nullptr, nullptr);
    pulseSystem()->pa_stream_unref(stream);
    qCWarning(lcAudioBackend)
        << "PulseAudioBackend: failed to connect input level monitor stream";
    return false;
  }

  level_stream = stream;
  level_stream_warned = false;
  return true;
}

// Assumes the threaded mainloop lock is already held by the caller.
void PulseAudioBackend::Impl::stopLevelStreamLocked() {
  if (level_stream == nullptr) {
    return;
  }
  pulseSystem()->pa_stream_set_state_callback(level_stream, nullptr, nullptr);
  pulseSystem()->pa_stream_set_read_callback(level_stream, nullptr, nullptr);
  pulseSystem()->pa_stream_disconnect(level_stream);
  pulseSystem()->pa_stream_unref(level_stream);
  level_stream = nullptr;
}

void PulseAudioBackend::Impl::levelStreamStateCallback(pa_stream *stream,
                                                       void *userdata) {
  auto *impl = static_cast<Impl *>(userdata);
  const pa_stream_state_t state = pulseSystem()->pa_stream_get_state(stream);
  if (state == PA_STREAM_READY) {
    return;
  }
  if (state != PA_STREAM_FAILED) {
    return;
  }
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self] { emit self->inputLevelChanged(0); }, Qt::QueuedConnection);
  if (!impl->level_stream_warned) {
    impl->level_stream_warned = true;
    qCWarning(lcAudioBackend)
        << "PulseAudioBackend: input level monitor stream failed";
  }
  QMetaObject::invokeMethod(self, &PulseAudioBackend::retryInputLevelMonitor,
                            Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::levelStreamReadCallback(pa_stream *stream,
                                                      size_t /*length*/,
                                                      void *userdata) {
  auto *impl = static_cast<Impl *>(userdata);
  const void *data = nullptr;
  size_t bytes = 0;
  if (pulseSystem()->pa_stream_peek(stream, &data, &bytes) != 0) {
    return;
  }
  if (data == nullptr && bytes == 0) {
    return;
  }
  if (data == nullptr && bytes > 0) {
    pulseSystem()->pa_stream_drop(stream);
    return;
  }

  float sample = 0.0F;
  if (bytes >= sizeof(float)) {
    std::memcpy(&sample, data, sizeof(float));
  }
  const int level =
      std::clamp(static_cast<int>(std::abs(sample) * 100.0F), 0, 100);
  pulseSystem()->pa_stream_drop(stream);

  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, level] { emit self->inputLevelChanged(level); },
      Qt::QueuedConnection);
}

AudioStream
PulseAudioBackend::Impl::sinkInputToStream(const pa_sink_input_info *info) {
  AudioStream stream;
  stream.id = info->index;
  stream.name = QString::fromUtf8(info->name != nullptr ? info->name : "");
  const char *app_name =
      pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
  stream.application = QString::fromUtf8(app_name != nullptr ? app_name : "");
  const char *icon =
      pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ICON_NAME);
  stream.icon_name = QString::fromUtf8(icon != nullptr ? icon : "");
  stream.device = info->sink;
  stream.volume = volumeToPercent(pa_cvolume_avg(&info->volume));
  stream.muted = info->mute != 0;
  stream.type = AudioStreamType::SinkInput;
  return stream;
}

AudioStream PulseAudioBackend::Impl::sourceOutputToStream(
    const pa_source_output_info *info) {
  AudioStream stream;
  stream.id = info->index;
  stream.name = QString::fromUtf8(info->name != nullptr ? info->name : "");
  const char *app_name =
      pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
  stream.application = QString::fromUtf8(app_name != nullptr ? app_name : "");
  const char *icon =
      pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ICON_NAME);
  stream.icon_name = QString::fromUtf8(icon != nullptr ? icon : "");
  stream.device = info->source;
  stream.volume = volumeToPercent(pa_cvolume_avg(&info->volume));
  stream.muted = info->mute != 0;
  stream.type = AudioStreamType::SourceOutput;
  return stream;
}

// ============================================================
// Context lifecycle
// ============================================================

void PulseAudioBackend::Impl::onContextReady() {
  const int subscription_mask =
      PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
      PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT |
      PA_SUBSCRIPTION_MASK_SERVER;
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) — OR-ing valid
  // bitmask constants
  const auto subscription_flags =
      static_cast<pa_subscription_mask_t>(subscription_mask);
  pa_operation *subscribe_operation = pulseSystem()->pa_context_subscribe(
      context, subscription_flags, nullptr, nullptr);
  if (subscribe_operation != nullptr) {
    pulseSystem()->pa_operation_unref(subscribe_operation);
  }

  queryServerInfo();

  PulseAudioBackend *self = backend;
  QMetaObject::invokeMethod(
      self,
      [self] {
        emit self->availableChanged(true);
        self->onReconnectSucceeded();
      },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::teardownContext() {
  if (context == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(mainloop);
  stopLevelStreamLocked();
  pulseSystem()->pa_context_set_state_callback(context, nullptr, nullptr);
  pulseSystem()->pa_context_set_subscribe_callback(context, nullptr, nullptr);
  pulseSystem()->pa_context_disconnect(context);
  pulseSystem()->pa_context_unref(context);
  context = nullptr;
  pulseSystem()->threaded_mainloop_unlock(mainloop);
}

bool PulseAudioBackend::Impl::connectNewContext() {
  context = pulseSystem()->pa_context_new(api, "holonight-shell");
  if (context == nullptr) {
    qCWarning(lcAudioBackend) << "PulseAudioBackend: failed to create context";
    return false;
  }

  pulseSystem()->pa_context_set_state_callback(context, contextStateCallback,
                                               this);
  pulseSystem()->pa_context_set_subscribe_callback(context, subscribeCallback,
                                                   this);

  pulseSystem()->threaded_mainloop_lock(mainloop);
  const bool connected =
      pulseSystem()->pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS,
                                        nullptr) >= 0;
  if (!connected) {
    qCWarning(lcAudioBackend)
        << "PulseAudioBackend: pa_context_connect failed:"
        << pa_strerror(pulseSystem()->pa_context_errno(context));
  }
  pulseSystem()->threaded_mainloop_unlock(mainloop);
  return connected;
}

void PulseAudioBackend::Impl::queryServerInfo() {
  pa_operation *operation = pulseSystem()->pa_context_get_server_info(
      context, serverInfoCallback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::querySinks() {
  pa_operation *operation = pulseSystem()->pa_context_get_sink_info_list(
      context, sinkListCallback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::querySources() {
  pa_operation *operation = pulseSystem()->pa_context_get_source_info_list(
      context, sourceListCallback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::querySinkInputs() {
  pa_operation *operation = pulseSystem()->pa_context_get_sink_input_info_list(
      context, sinkInputListCallback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::querySourceOutputs() {
  pa_operation *operation =
      pulseSystem()->pa_context_get_source_output_info_list(
          context, sourceOutputListCallback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

// ============================================================
// Subscribe event dispatch
// ============================================================

void PulseAudioBackend::Impl::handleSinkEvent(pa_context *ctx, int evt,
                                              uint32_t idx) {
  if (evt == PA_SUBSCRIPTION_EVENT_REMOVE) {
    metadata_warned_ids.remove(
        (static_cast<quint64>(AudioDeviceType::Sink) << 32U) | idx);
    PulseAudioBackend *self = backend;
    QMetaObject::invokeMethod(
        self, [self, idx] { emit self->sinkRemoved(idx); },
        Qt::QueuedConnection);
    return;
  }
  using SinkInfoCb = pa_sink_info_cb_t;
  SinkInfoCb callback = (evt == PA_SUBSCRIPTION_EVENT_NEW)
                            ? sinkListCallback
                            : sinkChangedCallback;
  pa_operation *operation = pulseSystem()->pa_context_get_sink_info_by_index(
      ctx, idx, callback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::handleSourceEvent(pa_context *ctx, int evt,
                                                uint32_t idx) {
  if (evt == PA_SUBSCRIPTION_EVENT_REMOVE) {
    metadata_warned_ids.remove(
        (static_cast<quint64>(AudioDeviceType::Source) << 32U) | idx);
    PulseAudioBackend *self = backend;
    QMetaObject::invokeMethod(
        self, [self, idx] { emit self->sourceRemoved(idx); },
        Qt::QueuedConnection);
    return;
  }
  using SourceInfoCb = pa_source_info_cb_t;
  SourceInfoCb callback = (evt == PA_SUBSCRIPTION_EVENT_NEW)
                              ? sourceListCallback
                              : sourceChangedCallback;
  pa_operation *operation = pulseSystem()->pa_context_get_source_info_by_index(
      ctx, idx, callback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::handleSinkInputEvent(pa_context *ctx, int evt,
                                                   uint32_t idx) {
  if (evt == PA_SUBSCRIPTION_EVENT_REMOVE) {
    PulseAudioBackend *self = backend;
    QMetaObject::invokeMethod(
        self, [self, idx] { emit self->streamRemoved(idx); },
        Qt::QueuedConnection);
    return;
  }
  using SinkInputInfoCb = pa_sink_input_info_cb_t;
  SinkInputInfoCb callback = (evt == PA_SUBSCRIPTION_EVENT_NEW)
                                 ? sinkInputListCallback
                                 : sinkInputChangedCallback;
  pa_operation *operation =
      pulseSystem()->pa_context_get_sink_input_info(ctx, idx, callback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::handleSourceOutputEvent(pa_context *ctx, int evt,
                                                      uint32_t idx) {
  if (evt == PA_SUBSCRIPTION_EVENT_REMOVE) {
    PulseAudioBackend *self = backend;
    QMetaObject::invokeMethod(
        self, [self, idx] { emit self->streamRemoved(idx); },
        Qt::QueuedConnection);
    return;
  }
  using SourceOutputInfoCb = pa_source_output_info_cb_t;
  SourceOutputInfoCb callback = (evt == PA_SUBSCRIPTION_EVENT_NEW)
                                    ? sourceOutputListCallback
                                    : sourceOutputChangedCallback;
  pa_operation *operation = pulseSystem()->pa_context_get_source_output_info(
      ctx, idx, callback, this);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

// ============================================================
// PA static callbacks
// ============================================================

void PulseAudioBackend::Impl::contextStateCallback(pa_context *ctx,
                                                   void *userdata) {
  auto *impl = static_cast<Impl *>(userdata);
  switch (pulseSystem()->pa_context_get_state(ctx)) {
  case PA_CONTEXT_READY:
    impl->onContextReady();
    break;
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED: {
    PulseAudioBackend *self = impl->backend;
    QMetaObject::invokeMethod(
        self,
        [self] {
          emit self->availableChanged(false);
          self->onContextLost();
        },
        Qt::QueuedConnection);
    break;
  }
  default:
    break;
  }
}

void PulseAudioBackend::Impl::subscribeCallback(
    pa_context *ctx, pa_subscription_event_type_t type, uint32_t idx,
    void *userdata) {
  auto *impl = static_cast<Impl *>(userdata);
  const int facility = static_cast<int>(type) &
                       static_cast<int>(PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
  const int evt = static_cast<int>(type) &
                  static_cast<int>(PA_SUBSCRIPTION_EVENT_TYPE_MASK);

  if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
    impl->handleSinkEvent(ctx, evt, idx);
  } else if (facility == PA_SUBSCRIPTION_EVENT_SOURCE) {
    impl->handleSourceEvent(ctx, evt, idx);
  } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
    impl->handleSinkInputEvent(ctx, evt, idx);
  } else if (facility == PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT) {
    impl->handleSourceOutputEvent(ctx, evt, idx);
  } else if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
    impl->queryServerInfo();
  }
}

void PulseAudioBackend::Impl::serverInfoCallback(pa_context * /*ctx*/,
                                                 const pa_server_info *info,
                                                 void *userdata) {
  if (info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  const QString new_default_source_name = QString::fromUtf8(
      info->default_source_name != nullptr ? info->default_source_name : "");
  const bool source_changed =
      impl->level_stream != nullptr &&
      new_default_source_name != impl->default_source_name;
  impl->default_sink_name = QString::fromUtf8(
      info->default_sink_name != nullptr ? info->default_sink_name : "");
  impl->default_source_name = new_default_source_name;
  impl->querySinks();
  impl->querySources();
  impl->querySinkInputs();
  impl->querySourceOutputs();
  if (source_changed) {
    impl->stopLevelStreamLocked();
    (void)impl->startLevelStreamLocked();
  }
}

void PulseAudioBackend::Impl::sinkListCallback(pa_context * /*ctx*/,
                                               const pa_sink_info *info,
                                               int eol, void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioDevice dev = impl->sinkToDevice(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, dev] { emit self->deviceAdded(dev); }, Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sourceListCallback(pa_context * /*ctx*/,
                                                 const pa_source_info *info,
                                                 int eol, void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  if (info->monitor_of_sink != PA_INVALID_INDEX) {
    return; // skip monitor sources (loopback of a sink) — not real input
            // devices
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioDevice dev = impl->sourceToDevice(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, dev] { emit self->deviceAdded(dev); }, Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sinkInputListCallback(
    pa_context * /*ctx*/, const pa_sink_input_info *info, int eol,
    void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioStream stream = sinkInputToStream(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, stream] { emit self->streamAdded(stream); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sourceOutputListCallback(
    pa_context * /*ctx*/, const pa_source_output_info *info, int eol,
    void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioStream stream = sourceOutputToStream(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, stream] { emit self->streamAdded(stream); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sinkChangedCallback(pa_context * /*ctx*/,
                                                  const pa_sink_info *info,
                                                  int eol, void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioDevice dev = impl->sinkToDevice(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, dev] { emit self->deviceChanged(dev); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sourceChangedCallback(pa_context * /*ctx*/,
                                                    const pa_source_info *info,
                                                    int eol, void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  if (info->monitor_of_sink != PA_INVALID_INDEX) {
    return; // skip monitor sources (loopback of a sink) — not real input
            // devices
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioDevice dev = impl->sourceToDevice(info);
  PulseAudioBackend *self = impl->backend;
  QMetaObject::invokeMethod(
      self, [self, dev] { emit self->deviceChanged(dev); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sinkInputChangedCallback(
    pa_context * /*ctx*/, const pa_sink_input_info *info, int eol,
    void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioStream stream = sinkInputToStream(info);
  PulseAudioBackend *self = impl->backend;
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  QMetaObject::invokeMethod(
      self, [self, stream] { emit self->streamChanged(stream); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::sourceOutputChangedCallback(
    pa_context * /*ctx*/, const pa_source_output_info *info, int eol,
    void *userdata) {
  if (eol != 0 || info == nullptr) {
    return;
  }
  auto *impl = static_cast<Impl *>(userdata);
  AudioStream stream = sourceOutputToStream(info);
  PulseAudioBackend *self = impl->backend;
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  QMetaObject::invokeMethod(
      self, [self, stream] { emit self->streamChanged(stream); },
      Qt::QueuedConnection);
}

void PulseAudioBackend::Impl::setSinkVolumeCallback(pa_context *ctx,
                                                    const pa_sink_info *info,
                                                    int eol, void *userdata) {
  auto *request = static_cast<VolumeRequest *>(userdata);
  if (eol != 0 || info == nullptr) {
    delete request;
    return;
  }
  pa_cvolume volume;
  pa_cvolume_set(&volume,
                 std::max(1U, static_cast<unsigned>(info->volume.channels)),
                 volumeFromPercent(request->percent));
  pa_operation *operation = pulseSystem()->pa_context_set_sink_volume_by_index(
      ctx, info->index, &volume, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::setSourceVolumeCallback(
    pa_context *ctx, const pa_source_info *info, int eol, void *userdata) {
  auto *request = static_cast<VolumeRequest *>(userdata);
  if (eol != 0 || info == nullptr) {
    delete request;
    return;
  }
  pa_cvolume volume;
  pa_cvolume_set(&volume,
                 std::max(1U, static_cast<unsigned>(info->volume.channels)),
                 volumeFromPercent(request->percent));
  pa_operation *operation =
      pulseSystem()->pa_context_set_source_volume_by_index(
          ctx, info->index, &volume, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

void PulseAudioBackend::Impl::setSinkInputVolumeCallback(
    pa_context *ctx, const pa_sink_input_info *info, int eol, void *userdata) {
  auto *request = static_cast<VolumeRequest *>(userdata);
  if (eol != 0 || info == nullptr) {
    delete request;
    return;
  }
  pa_cvolume volume;
  pa_cvolume_set(&volume,
                 std::max(1U, static_cast<unsigned>(info->volume.channels)),
                 volumeFromPercent(request->percent));
  pa_operation *operation = pulseSystem()->pa_context_set_sink_input_volume(
      ctx, info->index, &volume, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
}

// ============================================================
// PulseAudioBackend public methods
// ============================================================

PulseAudioBackend::PulseAudioBackend(QObject *parent)
    : AudioBackend(parent), reconnect_timer_(new QTimer(this)),
      impl_(std::make_unique<Impl>()) {
  impl_->backend = this;
  reconnect_timer_->setSingleShot(true);
  connect(reconnect_timer_, &QTimer::timeout, this,
          &PulseAudioBackend::attemptReconnect);
}

PulseAudioBackend::~PulseAudioBackend() { stop(); }

void PulseAudioBackend::start() {
  if (impl_->started) {
    return;
  }
  impl_->started = true;

  impl_->mainloop = pulseSystem()->threaded_mainloop_new();
  impl_->api = impl_->mainloop != nullptr
                   ? pulseSystem()->threaded_mainloop_get_api(impl_->mainloop)
                   : nullptr;

  if (impl_->mainloop == nullptr) {
    qCWarning(lcAudioBackend) << "PulseAudioBackend: failed to create mainloop";
    return;
  }
  if (impl_->api == nullptr) {
    qCWarning(lcAudioBackend)
        << "PulseAudioBackend: failed to get mainloop api";
    return;
  }

  impl_->mainloop_running =
      pulseSystem()->threaded_mainloop_start(impl_->mainloop) >= 0;
  if (!impl_->mainloop_running) {
    qCWarning(lcAudioBackend) << "PulseAudioBackend: failed to start mainloop";
    return;
  }

  if (!impl_->connectNewContext()) {
    onContextLost();
  }
}

void PulseAudioBackend::onContextLost() {
  if (reconnect_timer_->isActive()) {
    return;
  }
  if (health_state_ == AudioHealthState::Failed) {
    return;
  }
  setHealthState(AudioHealthState::Reconnecting);
  scheduleReconnect();
}

void PulseAudioBackend::onReconnectSucceeded() {
  reconnect_attempt_ = 0;
  reconnect_timer_->stop();
  setHealthState(AudioHealthState::Connected);
}

void PulseAudioBackend::attemptReconnect() {
  impl_->teardownContext();
  if (!impl_->connectNewContext()) {
    onContextLost();
  }
}

void PulseAudioBackend::scheduleReconnect() {
  if (reconnect_attempt_ >= kMaxReconnectAttempts) {
    setHealthState(AudioHealthState::Failed);
    return;
  }
  const auto schedule_index = std::min(static_cast<size_t>(reconnect_attempt_),
                                       s_reconnect_backoff_ms.size() - 1);
  const int delay_ms = s_reconnect_backoff_ms[schedule_index];
  ++reconnect_attempt_;
  reconnect_timer_->start(delay_ms);
}

void PulseAudioBackend::setHealthState(AudioHealthState state) {
  if (health_state_ == state) {
    return;
  }
  health_state_ = state;
  emit healthStateChanged(health_state_);
}

void PulseAudioBackend::startInputLevelMonitor() {
  if (impl_->mainloop == nullptr) {
    return;
  }
  impl_->level_monitor_requested = true;
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  const bool started = impl_->startLevelStreamLocked();
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
  if (!started) {
    QMetaObject::invokeMethod(this, &PulseAudioBackend::retryInputLevelMonitor,
                              Qt::QueuedConnection);
  }
}

void PulseAudioBackend::stopInputLevelMonitor() {
  impl_->level_monitor_requested = false;
  impl_->level_stream_retry_count = 0;
  if (impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  impl_->stopLevelStreamLocked();
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);

  PulseAudioBackend *self = this;
  QMetaObject::invokeMethod(
      self, [self] { emit self->inputLevelChanged(0); }, Qt::QueuedConnection);
}

void PulseAudioBackend::retryInputLevelMonitor() {
  constexpr int kMaximumLevelStreamRetries = 3;
  if (!impl_->level_monitor_requested || impl_->mainloop == nullptr ||
      impl_->level_stream_retry_count >= kMaximumLevelStreamRetries) {
    return;
  }

  ++impl_->level_stream_retry_count;
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  impl_->stopLevelStreamLocked();
  const bool started = impl_->startLevelStreamLocked();
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
  if (!started) {
    QMetaObject::invokeMethod(this, &PulseAudioBackend::retryInputLevelMonitor,
                              Qt::QueuedConnection);
  }
}

void PulseAudioBackend::stop() {
  reconnect_timer_->stop();
  impl_->level_monitor_requested = false;
  impl_->level_stream_retry_count = 0;
  if (impl_->mainloop == nullptr) {
    return;
  }
  if (impl_->context != nullptr) {
    pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
    impl_->stopLevelStreamLocked();
    pulseSystem()->pa_context_set_state_callback(impl_->context, nullptr,
                                                 nullptr);
    pulseSystem()->pa_context_set_subscribe_callback(impl_->context, nullptr,
                                                     nullptr);
    pulseSystem()->pa_context_disconnect(impl_->context);
    pulseSystem()->pa_context_unref(impl_->context);
    pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
  }
  if (impl_->mainloop_running) {
    pulseSystem()->threaded_mainloop_stop(impl_->mainloop);
  }
  pulseSystem()->threaded_mainloop_free(impl_->mainloop);
  impl_->mainloop = nullptr;
  impl_->api = nullptr;
  impl_->context = nullptr;
  impl_->mainloop_running = false;
  impl_->started = false;
}

void PulseAudioBackend::setDeviceVolume(uint32_t idx, int percent) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  auto request =
      std::make_unique<Impl::VolumeRequest>(Impl::VolumeRequest{percent});
  auto *request_data = request.release();
  pa_operation *operation = pulseSystem()->pa_context_get_sink_info_by_index(
      impl_->context, idx, Impl::setSinkVolumeCallback, request_data);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  } else {
    delete request_data;
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setDeviceMuted(uint32_t idx, bool muted) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_sink_mute_by_index(
      impl_->context, idx, muted ? 1 : 0, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setSourceVolume(uint32_t idx, int percent) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  auto request =
      std::make_unique<Impl::VolumeRequest>(Impl::VolumeRequest{percent});
  auto *request_data = request.release();
  pa_operation *operation = pulseSystem()->pa_context_get_source_info_by_index(
      impl_->context, idx, Impl::setSourceVolumeCallback, request_data);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  } else {
    delete request_data;
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setSourceMuted(uint32_t idx, bool muted) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_source_mute_by_index(
      impl_->context, idx, muted ? 1 : 0, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setDefaultOutput(uint32_t idx) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  const QString name = QString::number(idx);
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_default_sink(
      impl_->context, name.toUtf8().constData(), nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setDefaultInput(uint32_t idx) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  const QString name = QString::number(idx);
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_default_source(
      impl_->context, name.toUtf8().constData(), nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setDefaultOutputByName(const QString &name) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_default_sink(
      impl_->context, name.toUtf8().constData(), nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setDefaultInputByName(const QString &name) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_default_source(
      impl_->context, name.toUtf8().constData(), nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setStreamVolume(uint32_t idx, int percent) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  auto request =
      std::make_unique<Impl::VolumeRequest>(Impl::VolumeRequest{percent});
  auto *request_data = request.release();
  pa_operation *operation = pulseSystem()->pa_context_get_sink_input_info(
      impl_->context, idx, Impl::setSinkInputVolumeCallback, request_data);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  } else {
    delete request_data;
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::setStreamMuted(uint32_t idx, bool muted) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_set_sink_input_mute(
      impl_->context, idx, muted ? 1 : 0, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

void PulseAudioBackend::moveStreamToDevice(uint32_t stream_idx,
                                           uint32_t device_idx) {
  if (impl_->context == nullptr || impl_->mainloop == nullptr) {
    return;
  }
  pulseSystem()->threaded_mainloop_lock(impl_->mainloop);
  pa_operation *operation = pulseSystem()->pa_context_move_sink_input_by_index(
      impl_->context, stream_idx, device_idx, nullptr, nullptr);
  if (operation != nullptr) {
    pulseSystem()->pa_operation_unref(operation);
  }
  pulseSystem()->threaded_mainloop_unlock(impl_->mainloop);
}

} // namespace HoloNight::System
