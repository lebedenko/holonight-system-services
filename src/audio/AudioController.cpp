#include "AudioController.h"

#include "PulseAudioBackend.h"

#include <QCoreApplication>

namespace HoloNight::System {

AudioController::AudioController(QObject *parent)
    : AudioController(std::make_unique<PulseAudioBackend>(), parent) {}

AudioController::AudioController(std::unique_ptr<AudioBackend> backend,
                                 QObject *parent)
    : QObject(parent), outputs_(new AudioDeviceModel(this)),
      inputs_(new AudioDeviceModel(this)),
      playback_streams_(new AudioStreamModel(this)),
      recording_streams_(new AudioStreamModel(this)),
      backend_(std::move(backend)) {}

AudioController::AudioController([[maybe_unused]] SkipInitTag tag,
                                 QObject *parent)
    : QObject(parent), outputs_(new AudioDeviceModel(this)),
      inputs_(new AudioDeviceModel(this)),
      playback_streams_(new AudioStreamModel(this)),
      recording_streams_(new AudioStreamModel(this)) {}

AudioController::~AudioController() {
  if (backend_ != nullptr) {
    backend_->stop();
  }
  QCoreApplication::removePostedEvents(this);
}

void AudioController::start() {
  if (started_) {
    return;
  }
  started_ = true;

  if (backend_ == nullptr) {
    return;
  }

  connect(backend_.get(), &AudioBackend::availableChanged, this,
          &AudioController::setAvailable);
  connect(backend_.get(), &AudioBackend::healthStateChanged, this,
          &AudioController::setHealthState);
  connect(backend_.get(), &AudioBackend::deviceAdded, this,
          &AudioController::onDeviceAdded);
  connect(backend_.get(), &AudioBackend::deviceChanged, this,
          &AudioController::onDeviceChanged);
  connect(backend_.get(), &AudioBackend::sinkRemoved, this,
          &AudioController::onSinkRemoved);
  connect(backend_.get(), &AudioBackend::sourceRemoved, this,
          &AudioController::onSourceRemoved);
  connect(backend_.get(), &AudioBackend::streamAdded, this,
          &AudioController::onStreamAdded);
  connect(backend_.get(), &AudioBackend::streamRemoved, this,
          &AudioController::onStreamRemoved);
  connect(backend_.get(), &AudioBackend::streamChanged, this,
          &AudioController::onStreamChanged);
  connect(backend_.get(), &AudioBackend::inputLevelChanged, this,
          &AudioController::applyInputLevel);

  backend_->start();
}

void AudioController::setVolume(int percent) {
  if (backend_ == nullptr || default_output_id_ == kInvalidId) {
    return;
  }
  backend_->setDeviceVolume(default_output_id_, percent);
}

void AudioController::setDefaultOutput(uint32_t idx) {
  if (backend_ != nullptr) {
    backend_->setDefaultOutput(idx);
  }
}

void AudioController::setDefaultInput(uint32_t idx) {
  if (backend_ != nullptr) {
    backend_->setDefaultInput(idx);
  }
}

void AudioController::setDefaultOutputByName(const QString &name) {
  if (backend_ != nullptr) {
    backend_->setDefaultOutputByName(name);
  }
}

void AudioController::setDefaultInputByName(const QString &name) {
  if (backend_ != nullptr) {
    backend_->setDefaultInputByName(name);
  }
}

void AudioController::setDeviceVolume(uint32_t idx, int percent) {
  if (backend_ != nullptr) {
    backend_->setDeviceVolume(idx, percent);
  }
}

void AudioController::setDeviceMuted(uint32_t idx, bool muted) {
  if (backend_ != nullptr) {
    backend_->setDeviceMuted(idx, muted);
  }
}

void AudioController::setDefaultOutputMuted(bool muted) {
  if (backend_ == nullptr || default_output_id_ == kInvalidId) {
    return;
  }
  backend_->setDeviceMuted(default_output_id_, muted);
}

void AudioController::setInputDeviceVolume(uint32_t idx, int percent) {
  if (backend_ != nullptr) {
    backend_->setSourceVolume(idx, percent);
  }
}

void AudioController::setInputDeviceMuted(uint32_t idx, bool muted) {
  if (backend_ != nullptr) {
    backend_->setSourceMuted(idx, muted);
  }
}

void AudioController::setStreamVolume(uint32_t idx, int percent) {
  if (backend_ != nullptr) {
    backend_->setStreamVolume(idx, percent);
  }
}

void AudioController::setStreamMuted(uint32_t idx, bool muted) {
  if (backend_ != nullptr) {
    backend_->setStreamMuted(idx, muted);
  }
}

void AudioController::moveStreamToOutput(uint32_t stream_idx,
                                         uint32_t sink_idx) {
  if (backend_ != nullptr) {
    backend_->moveStreamToDevice(stream_idx, sink_idx);
  }
}

void AudioController::moveStreamToInput(uint32_t stream_idx,
                                        uint32_t source_idx) {
  if (backend_ != nullptr) {
    backend_->moveStreamToDevice(stream_idx, source_idx);
  }
}

void AudioController::startInputLevelMonitoring() {
  ++input_level_monitoring_users_;
  if (backend_ != nullptr && input_level_monitoring_users_ == 1) {
    backend_->startInputLevelMonitor();
  }
}

void AudioController::stopInputLevelMonitoring() {
  if (input_level_monitoring_users_ == 0) {
    return;
  }
  --input_level_monitoring_users_;
  if (backend_ != nullptr && input_level_monitoring_users_ == 0) {
    backend_->stopInputLevelMonitor();
  }
}

void AudioController::applyVolume(int value) {
  if (volume_ == value) {
    return;
  }
  volume_ = value;
  emit volumeChanged();
}

void AudioController::applyMuted(bool value) {
  if (muted_ == value) {
    return;
  }
  muted_ = value;
  emit mutedChanged();
}

void AudioController::applyInputLevel(int value) {
  if (input_level_ == value) {
    return;
  }
  input_level_ = value;
  emit inputLevelChanged(input_level_);
}

void AudioController::setAvailable(bool value) {
  if (available_ == value) {
    return;
  }
  available_ = value;
  emit availableChanged();
}

void AudioController::onDeviceAdded(const AudioDevice &device) {
  AudioDeviceModel *model =
      (device.type == AudioDeviceType::Sink) ? outputs_ : inputs_;
  model->applyAdd(device);
  if (device.is_default) {
    applyDefaultDeviceState(device);
  }
}

void AudioController::onDeviceChanged(const AudioDevice &device) {
  AudioDeviceModel *model =
      (device.type == AudioDeviceType::Sink) ? outputs_ : inputs_;
  model->applyChange(device);
  if (device.is_default) {
    applyDefaultDeviceState(device);
  }
}

void AudioController::onSinkRemoved(uint32_t idx) {
  outputs_->applyRemove(idx);
  if (idx == default_output_id_) {
    default_output_id_ = kInvalidId;
    emit defaultOutputIdChanged();
  }
}

void AudioController::onSourceRemoved(uint32_t idx) {
  inputs_->applyRemove(idx);
}

void AudioController::onStreamAdded(const AudioStream &stream) {
  AudioStreamModel *model = (stream.type == AudioStreamType::SinkInput)
                                ? playback_streams_
                                : recording_streams_;
  model->applyAdd(stream);
}

void AudioController::onStreamChanged(const AudioStream &stream) {
  AudioStreamModel *model = (stream.type == AudioStreamType::SinkInput)
                                ? playback_streams_
                                : recording_streams_;
  model->applyChange(stream);
}

void AudioController::onStreamRemoved(uint32_t idx) {
  playback_streams_->applyRemove(idx);
  recording_streams_->applyRemove(idx);
}

void AudioController::setHealthState(AudioHealthState value) {
  if (health_state_ == value) {
    return;
  }
  health_state_ = value;
  emit healthStateChanged();
}

void AudioController::applyDefaultDeviceState(const AudioDevice &device) {
  if (device.type == AudioDeviceType::Sink) {
    if (default_output_id_ != device.id) {
      default_output_id_ = device.id;
      emit defaultOutputIdChanged();
    }
    applyVolume(device.volume);
    applyMuted(device.muted);
  }
}

} // namespace HoloNight::System
