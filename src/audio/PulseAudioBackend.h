#pragma once

#include "AudioBackend.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <pulse/proplist.h>
#include <vector>

namespace HoloNight::System {

class PulseAudioSystem;

// Free function (external linkage) so test_pulse_audio_backend.cpp can exercise
// it directly against hand-built pa_proplist fixtures without going through a
// live PulseAudio connection.
QString classifyBusType(const pa_proplist *proplist, const QString &device_name,
                        const QString &active_port_name);

class PulseAudioBackend : public AudioBackend {
  Q_OBJECT

public:
  explicit PulseAudioBackend(QObject *parent = nullptr);
  ~PulseAudioBackend() override;

  PulseAudioBackend(const PulseAudioBackend &) = delete;
  PulseAudioBackend &operator=(const PulseAudioBackend &) = delete;
  PulseAudioBackend(PulseAudioBackend &&) = delete;
  PulseAudioBackend &operator=(PulseAudioBackend &&) = delete;

  static void setPulseAudioSystem(PulseAudioSystem *sys);
  static void resetPulseAudioSystem();

  // Test seam only — overrides the production backoff schedule (default:
  // 1s,2s,4s,8s,16s,30s-cap) so reconnect-timing tests don't block on real
  // wall-clock delays.
  static void setReconnectBackoffScheduleForTests(std::vector<int> delays_ms);
  static void resetReconnectBackoffSchedule();

  void start() override;
  void stop() override;

  void startInputLevelMonitor() override;
  void stopInputLevelMonitor() override;

  void setDeviceVolume(uint32_t idx, int percent) override;
  void setDeviceMuted(uint32_t idx, bool muted) override;
  void setSourceVolume(uint32_t idx, int percent) override;
  void setSourceMuted(uint32_t idx, bool muted) override;
  void setDefaultOutput(uint32_t idx) override;
  void setDefaultInput(uint32_t idx) override;
  void setDefaultOutputByName(const QString &name) override;
  void setDefaultInputByName(const QString &name) override;
  void setStreamVolume(uint32_t idx, int percent) override;
  void setStreamMuted(uint32_t idx, bool muted) override;
  void moveStreamToDevice(uint32_t stream_idx, uint32_t device_idx) override;

private Q_SLOTS:
  void onContextLost();
  void onReconnectSucceeded();
  void attemptReconnect();
  void retryInputLevelMonitor();

private:
  void scheduleReconnect();
  void setHealthState(AudioHealthState state);

  static constexpr int kMaxReconnectAttempts = 8;

  int reconnect_attempt_{0};
  QTimer *reconnect_timer_{nullptr};
  AudioHealthState health_state_{AudioHealthState::Connected};

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace HoloNight::System
