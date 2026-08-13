#pragma once

#include <QString>

#include <cstdint>

namespace HoloNight::System {

enum class AudioDeviceType : uint8_t { Sink, Source };

enum class AudioStreamType : uint8_t { SinkInput, SourceOutput };

enum class AudioHealthState : uint8_t { Connected, Reconnecting, Failed };

struct AudioDevice {
  uint32_t id{0};
  QString name;
  QString description;
  uint8_t volume{0};
  bool muted{false};
  bool is_default{false};
  AudioDeviceType type{AudioDeviceType::Sink};
  QString bus_type;
  uint8_t channel_count{0};
  uint32_t sample_rate{0};
  QString codec;
  QString icon_name;
};

struct AudioStream {
  uint32_t id{0};
  QString name;
  QString application;
  QString icon_name;
  uint32_t device{0};
  uint8_t volume{0};
  bool muted{false};
  AudioStreamType type{AudioStreamType::SinkInput};
};

} // namespace HoloNight::System
