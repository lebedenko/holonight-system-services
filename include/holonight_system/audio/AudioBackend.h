#pragma once

#include "AudioTypes.h"

#include <QObject>
#include <QString>

#include <cstdint>

namespace HoloNight::System {

class AudioBackend : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~AudioBackend() override = default;

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void startInputLevelMonitor() = 0;
  virtual void stopInputLevelMonitor() = 0;
  virtual void setDeviceVolume(uint32_t idx, int percent) = 0;
  virtual void setDeviceMuted(uint32_t idx, bool muted) = 0;
  virtual void setSourceVolume(uint32_t idx, int percent) = 0;
  virtual void setSourceMuted(uint32_t idx, bool muted) = 0;
  virtual void setDefaultOutput(uint32_t idx) = 0;
  virtual void setDefaultInput(uint32_t idx) = 0;
  virtual void setDefaultOutputByName(const QString &name) = 0;
  virtual void setDefaultInputByName(const QString &name) = 0;
  virtual void setStreamVolume(uint32_t idx, int percent) = 0;
  virtual void setStreamMuted(uint32_t idx, bool muted) = 0;
  virtual void moveStreamToDevice(uint32_t stream_idx, uint32_t device_idx) = 0;

Q_SIGNALS:
  void deviceAdded(AudioDevice device);
  void sinkRemoved(uint32_t idx);
  void sourceRemoved(uint32_t idx);
  void deviceChanged(AudioDevice device);
  void streamAdded(AudioStream stream);
  void streamRemoved(uint32_t idx);
  void streamChanged(AudioStream stream);
  void availableChanged(bool available);
  void healthStateChanged(AudioHealthState state);
  void inputLevelChanged(int level);
};

} // namespace HoloNight::System
