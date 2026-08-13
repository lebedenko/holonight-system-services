#pragma once

#include "AudioBackend.h"
#include "AudioDeviceModel.h"
#include "AudioStreamModel.h"
#include "AudioTypes.h"

#include <QObject>

#include <cstdint>
#include <limits>
#include <memory>

namespace HoloNight::System {

class AudioController : public QObject {
  Q_OBJECT

  Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
  Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
  Q_PROPERTY(int healthState READ healthState NOTIFY healthStateChanged)
  Q_PROPERTY(quint32 defaultOutputId READ defaultOutputId NOTIFY
                 defaultOutputIdChanged)
  Q_PROPERTY(int inputLevel READ inputLevel NOTIFY inputLevelChanged)
  Q_PROPERTY(AudioDeviceModel *outputs READ outputs CONSTANT)
  Q_PROPERTY(AudioDeviceModel *inputs READ inputs CONSTANT)
  Q_PROPERTY(AudioStreamModel *playbackStreams READ playbackStreams CONSTANT)
  Q_PROPERTY(AudioStreamModel *recordingStreams READ recordingStreams CONSTANT)

public:
  struct SkipInitTag {};
  static constexpr SkipInitTag SkipInit{};

  explicit AudioController(QObject *parent = nullptr);
  explicit AudioController(std::unique_ptr<AudioBackend> backend,
                           QObject *parent = nullptr);
  explicit AudioController(SkipInitTag tag, QObject *parent = nullptr);
  ~AudioController() override;

  AudioController(const AudioController &) = delete;
  AudioController &operator=(const AudioController &) = delete;
  AudioController(AudioController &&) = delete;
  AudioController &operator=(AudioController &&) = delete;

  [[nodiscard]] int volume() const { return volume_; }
  [[nodiscard]] bool muted() const { return muted_; }
  [[nodiscard]] bool available() const { return available_; }
  [[nodiscard]] int healthState() const {
    return static_cast<int>(health_state_);
  }
  [[nodiscard]] quint32 defaultOutputId() const { return default_output_id_; }
  [[nodiscard]] int inputLevel() const { return input_level_; }
  [[nodiscard]] int inputLevelMonitoringUsers() const {
    return input_level_monitoring_users_;
  }
  [[nodiscard]] AudioDeviceModel *outputs() const { return outputs_; }
  [[nodiscard]] AudioDeviceModel *inputs() const { return inputs_; }
  [[nodiscard]] AudioStreamModel *playbackStreams() const {
    return playback_streams_;
  }
  [[nodiscard]] AudioStreamModel *recordingStreams() const {
    return recording_streams_;
  }

  void start();

  Q_INVOKABLE void setVolume(int percent);
  Q_INVOKABLE void setDefaultOutput(uint32_t idx);
  Q_INVOKABLE void setDefaultInput(uint32_t idx);
  Q_INVOKABLE void setDefaultOutputByName(const QString &name);
  Q_INVOKABLE void setDefaultInputByName(const QString &name);
  Q_INVOKABLE void setDeviceVolume(uint32_t idx, int percent);
  Q_INVOKABLE void setDeviceMuted(uint32_t idx, bool muted);
  Q_INVOKABLE void setDefaultOutputMuted(bool muted);
  Q_INVOKABLE void setInputDeviceVolume(uint32_t idx, int percent);
  Q_INVOKABLE void setInputDeviceMuted(uint32_t idx, bool muted);
  Q_INVOKABLE void setStreamVolume(uint32_t idx, int percent);
  Q_INVOKABLE void setStreamMuted(uint32_t idx, bool muted);
  Q_INVOKABLE void moveStreamToOutput(uint32_t stream_idx, uint32_t sink_idx);
  Q_INVOKABLE void moveStreamToInput(uint32_t stream_idx, uint32_t source_idx);
  Q_INVOKABLE void startInputLevelMonitoring();
  Q_INVOKABLE void stopInputLevelMonitoring();

  void applyVolume(int value);
  void applyMuted(bool value);
  void setAvailable(bool value);
  void applyInputLevel(int value);

  // Backend event handlers — also part of the test seam: GTest feeds
  // device/stream fixtures through these directly (an AudioController(SkipInit)
  // has no backend to emit them).
  void onDeviceAdded(const AudioDevice &device);
  void onDeviceChanged(const AudioDevice &device);
  void onSinkRemoved(uint32_t idx);
  void onSourceRemoved(uint32_t idx);
  void onStreamAdded(const AudioStream &stream);
  void onStreamChanged(const AudioStream &stream);
  void onStreamRemoved(uint32_t idx);

Q_SIGNALS:
  void volumeChanged();
  void mutedChanged();
  void availableChanged();
  void healthStateChanged();
  void defaultOutputIdChanged();
  void inputLevelChanged(int level);

private:
  void applyDefaultDeviceState(const AudioDevice &device);
  void setHealthState(AudioHealthState value);

  static constexpr uint32_t kInvalidId = std::numeric_limits<uint32_t>::max();

  std::unique_ptr<AudioBackend> backend_;
  AudioDeviceModel *outputs_;
  AudioDeviceModel *inputs_;
  AudioStreamModel *playback_streams_;
  AudioStreamModel *recording_streams_;
  uint32_t default_output_id_{kInvalidId};

  int input_level_{0};
  int input_level_monitoring_users_{0};
  int volume_{0};
  bool muted_{false};
  bool available_{false};
  bool started_{false};
  AudioHealthState health_state_{AudioHealthState::Connected};
};

} // namespace HoloNight::System
