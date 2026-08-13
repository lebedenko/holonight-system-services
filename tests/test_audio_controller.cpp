#include "AudioController.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

#include <memory>

using namespace HoloNight::System;

namespace {

struct BackendCalls {
  int starts{0};
  int stops{0};
  int volume{-1};
  uint32_t volume_device{0};
};

class FakeAudioBackend final : public AudioBackend {
public:
  explicit FakeAudioBackend(BackendCalls *calls) : calls_(calls) {}

  void start() override { ++calls_->starts; }
  void stop() override { ++calls_->stops; }
  void startInputLevelMonitor() override {}
  void stopInputLevelMonitor() override {}
  void setDeviceVolume(uint32_t idx, int percent) override {
    calls_->volume_device = idx;
    calls_->volume = percent;
  }
  void setDeviceMuted(uint32_t, bool) override {}
  void setSourceVolume(uint32_t, int) override {}
  void setSourceMuted(uint32_t, bool) override {}
  void setDefaultOutput(uint32_t) override {}
  void setDefaultInput(uint32_t) override {}
  void setDefaultOutputByName(const QString &) override {}
  void setDefaultInputByName(const QString &) override {}
  void setStreamVolume(uint32_t, int) override {}
  void setStreamMuted(uint32_t, bool) override {}
  void moveStreamToDevice(uint32_t, uint32_t) override {}

private:
  BackendCalls *calls_;
};

AudioDevice makeDevice(uint32_t dev_id, AudioDeviceType type,
                       uint8_t volume = 50, bool muted = false,
                       bool is_default = false) {
  AudioDevice dev;
  dev.id = dev_id;
  dev.name = QStringLiteral("device-%1").arg(dev_id);
  dev.description = QStringLiteral("Test device");
  dev.volume = volume;
  dev.muted = muted;
  dev.is_default = is_default;
  dev.type = type;
  return dev;
}

} // namespace

TEST(AudioController, InjectedBackendDrivesLiveStateAndReceivesControls) {
  BackendCalls calls;
  auto backend = std::make_unique<FakeAudioBackend>(&calls);
  FakeAudioBackend *backend_ptr = backend.get();

  {
    AudioController controller(std::move(backend));
    controller.start();
    emit backend_ptr->availableChanged(true);
    emit backend_ptr->deviceAdded(
        makeDevice(42, AudioDeviceType::Sink, 65, false, true));

    EXPECT_TRUE(controller.available());
    EXPECT_EQ(controller.outputs()->rowCount(), 1);
    EXPECT_EQ(controller.volume(), 65);
    EXPECT_EQ(calls.starts, 1);

    controller.setVolume(81);
    EXPECT_EQ(calls.volume_device, 42U);
    EXPECT_EQ(calls.volume, 81);

    emit backend_ptr->sinkRemoved(42);
    EXPECT_EQ(controller.outputs()->rowCount(), 0);
  }

  EXPECT_EQ(calls.stops, 1);
}

TEST(AudioController, ApplyVolumeMutedAndAvailableEmitSignalsForChanges) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy volume_changed(&service, &AudioController::volumeChanged);
  QSignalSpy muted_changed(&service, &AudioController::mutedChanged);
  QSignalSpy available_changed(&service, &AudioController::availableChanged);

  service.applyVolume(42);
  service.applyMuted(true);
  service.setAvailable(true);

  EXPECT_EQ(service.volume(), 42);
  EXPECT_TRUE(service.muted());
  EXPECT_TRUE(service.available());
  EXPECT_EQ(volume_changed.count(), 1);
  EXPECT_EQ(muted_changed.count(), 1);
  EXPECT_EQ(available_changed.count(), 1);
}

TEST(AudioController, ReapplyingSameStateDoesNotEmitSignals) {
  AudioController service(AudioController::SkipInit);
  service.applyVolume(42);
  service.applyMuted(true);
  service.setAvailable(true);

  QSignalSpy volume_changed(&service, &AudioController::volumeChanged);
  QSignalSpy muted_changed(&service, &AudioController::mutedChanged);
  QSignalSpy available_changed(&service, &AudioController::availableChanged);

  service.applyVolume(42);
  service.applyMuted(true);
  service.setAvailable(true);

  EXPECT_EQ(volume_changed.count(), 0);
  EXPECT_EQ(muted_changed.count(), 0);
  EXPECT_EQ(available_changed.count(), 0);
}

TEST(AudioController, SetVolumeIsNoOpWithoutBackend) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy volume_changed(&service, &AudioController::volumeChanged);

  service.setVolume(75);

  EXPECT_EQ(service.volume(), 0);
  EXPECT_EQ(volume_changed.count(), 0);
}

TEST(AudioController, SourceControlMethodsAreNoOpWithoutBackend) {
  AudioController service(AudioController::SkipInit);

  service.setInputDeviceVolume(1, 65);
  service.setInputDeviceMuted(1, true);

  EXPECT_FALSE(service.available());
}

TEST(AudioController, DefaultOutputStateTracksDefaultSink) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy default_output_changed(&service,
                                    &AudioController::defaultOutputIdChanged);
  QSignalSpy volume_changed(&service, &AudioController::volumeChanged);
  QSignalSpy muted_changed(&service, &AudioController::mutedChanged);

  service.onDeviceAdded(makeDevice(4, AudioDeviceType::Sink, 72, true, true));

  EXPECT_EQ(service.defaultOutputId(), 4U);
  EXPECT_EQ(service.volume(), 72);
  EXPECT_TRUE(service.muted());
  EXPECT_EQ(default_output_changed.count(), 1);
  EXPECT_EQ(volume_changed.count(), 1);
  EXPECT_EQ(muted_changed.count(), 1);
}

TEST(AudioController, DefaultOutputIgnoresDefaultSource) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy default_output_changed(&service,
                                    &AudioController::defaultOutputIdChanged);

  service.onDeviceAdded(
      makeDevice(9, AudioDeviceType::Source, 63, false, true));

  EXPECT_NE(service.defaultOutputId(), 9U);
  EXPECT_EQ(default_output_changed.count(), 0);
}

TEST(AudioController, StartIsIdempotent) {
  AudioController service(AudioController::SkipInit);

  service.start();
  service.start();

  EXPECT_FALSE(service.available());
}

TEST(AudioController, ModelsAreNonNull) {
  AudioController service(AudioController::SkipInit);

  EXPECT_NE(service.outputs(), nullptr);
  EXPECT_NE(service.inputs(), nullptr);
  EXPECT_NE(service.playbackStreams(), nullptr);
  EXPECT_NE(service.recordingStreams(), nullptr);
}

TEST(AudioController, SinkRemovalOnlyAffectsOutputsModel) {
  AudioController service(AudioController::SkipInit);

  service.onDeviceAdded(makeDevice(1, AudioDeviceType::Sink));
  service.onDeviceAdded(makeDevice(2, AudioDeviceType::Sink));
  service.onDeviceAdded(makeDevice(1, AudioDeviceType::Source));
  service.onDeviceAdded(makeDevice(2, AudioDeviceType::Source));
  ASSERT_EQ(service.outputs()->rowCount(), 2);
  ASSERT_EQ(service.inputs()->rowCount(), 2);

  // A sink and a source can legitimately share the same PulseAudio index —
  // removing sink id 1 must not touch a source that happens to also have id 1
  // (this is the exact bug REQ-F-005/006 exist to fix).
  service.onSinkRemoved(1);

  EXPECT_EQ(service.outputs()->rowCount(), 1);
  EXPECT_EQ(service.inputs()->rowCount(), 2);
}

TEST(AudioController, SourceRemovalOnlyAffectsInputsModel) {
  AudioController service(AudioController::SkipInit);

  service.onDeviceAdded(makeDevice(1, AudioDeviceType::Sink));
  service.onDeviceAdded(makeDevice(2, AudioDeviceType::Sink));
  service.onDeviceAdded(makeDevice(1, AudioDeviceType::Source));
  service.onDeviceAdded(makeDevice(2, AudioDeviceType::Source));
  ASSERT_EQ(service.outputs()->rowCount(), 2);
  ASSERT_EQ(service.inputs()->rowCount(), 2);

  service.onSourceRemoved(1);

  EXPECT_EQ(service.outputs()->rowCount(), 2);
  EXPECT_EQ(service.inputs()->rowCount(), 1);
}

TEST(AudioController, SinkRemovalOfDefaultOutputClearsDefaultOutputId) {
  AudioController service(AudioController::SkipInit);
  service.onDeviceAdded(makeDevice(4, AudioDeviceType::Sink, 72, true, true));
  ASSERT_EQ(service.defaultOutputId(), 4U);

  QSignalSpy default_output_changed(&service,
                                    &AudioController::defaultOutputIdChanged);
  service.onSinkRemoved(4);

  EXPECT_NE(service.defaultOutputId(), 4U);
  EXPECT_EQ(default_output_changed.count(), 1);
}

TEST(AudioController, SetDefaultOutputMutedIsNoOpWithoutBackend) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy muted_changed(&service, &AudioController::mutedChanged);

  service.setDefaultOutputMuted(true);

  EXPECT_FALSE(service.muted());
  EXPECT_EQ(muted_changed.count(), 0);
}

TEST(AudioController, SetDefaultOutputMutedIsNoOpWithInvalidDefaultOutputId) {
  AudioController service;
  QSignalSpy muted_changed(&service, &AudioController::mutedChanged);

  service.setDefaultOutputMuted(true);

  EXPECT_FALSE(service.muted());
  EXPECT_EQ(muted_changed.count(), 0);
}

TEST(AudioController, StartStopInputLevelMonitoringAreNoOpWithoutBackend) {
  AudioController service(AudioController::SkipInit);

  service.startInputLevelMonitoring();
  service.stopInputLevelMonitoring();

  EXPECT_EQ(service.inputLevel(), 0);
}

TEST(AudioController, InputLevelMonitoringRemainsAcquiredUntilLastUserStops) {
  AudioController service(AudioController::SkipInit);

  service.startInputLevelMonitoring();
  service.startInputLevelMonitoring();
  EXPECT_EQ(service.inputLevelMonitoringUsers(), 2);

  service.stopInputLevelMonitoring();
  EXPECT_EQ(service.inputLevelMonitoringUsers(), 1);

  service.stopInputLevelMonitoring();
  service.stopInputLevelMonitoring();
  EXPECT_EQ(service.inputLevelMonitoringUsers(), 0);
}

TEST(AudioController, InputLevelStartsAtZero) {
  AudioController service(AudioController::SkipInit);

  EXPECT_EQ(service.inputLevel(), 0);
}

TEST(AudioController, ApplyInputLevelEmitsOnChangeAndSuppressesRepeat) {
  AudioController service(AudioController::SkipInit);
  QSignalSpy input_level_changed(&service, &AudioController::inputLevelChanged);

  service.applyInputLevel(42);
  service.applyInputLevel(42);

  EXPECT_EQ(service.inputLevel(), 42);
  EXPECT_EQ(input_level_changed.count(), 1);
  EXPECT_EQ(input_level_changed.first().at(0).toInt(), 42);
}
