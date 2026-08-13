#include "AudioDeviceModel.h"

using namespace HoloNight::System;

#include <QSignalSpy>

#include <gtest/gtest.h>

namespace {

AudioDevice makeSink(uint32_t dev_id, const char *name, uint8_t volume = 50,
                     bool muted = false, bool is_default = false,
                     const QString &bus_type = QString(),
                     uint8_t channel_count = 0, uint32_t sample_rate = 0,
                     const QString &codec = QString(),
                     const QString &icon_name = QString()) {
  AudioDevice dev;
  dev.id = dev_id;
  dev.name = QString::fromUtf8(name);
  dev.description = QStringLiteral("Test description");
  dev.display_name = QStringLiteral("Test display name");
  dev.raw_description = dev.description;
  dev.vendor_name = QStringLiteral("Test vendor");
  dev.product_name = QStringLiteral("Test product");
  dev.port_name = QStringLiteral("analog-output-speaker");
  dev.port_description = QStringLiteral("Speakers");
  dev.form_factor = QStringLiteral("internal");
  dev.volume = volume;
  dev.muted = muted;
  dev.is_default = is_default;
  dev.type = AudioDeviceType::Sink;
  dev.bus_type = bus_type;
  dev.channel_count = channel_count;
  dev.sample_rate = sample_rate;
  dev.codec = codec;
  dev.icon_name = icon_name;
  return dev;
}

AudioDevice makeSource(uint32_t dev_id, const char *name, uint8_t volume = 50,
                       bool muted = false, bool is_default = false) {
  AudioDevice dev = makeSink(dev_id, name, volume, muted, is_default);
  dev.type = AudioDeviceType::Source;
  return dev;
}

} // namespace

TEST(AudioDeviceModel, RowCountIsZeroInitially) {
  AudioDeviceModel model;
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(AudioDeviceModel, ApplyAddIncreasesRowCount) {
  AudioDeviceModel model;
  QSignalSpy rows_inserted(&model, &AudioDeviceModel::rowsInserted);

  model.applyAdd(makeSink(1, "sink1"));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 1);
}

TEST(AudioDeviceModel, ApplyAddUpdatesExistingRowByDeviceId) {
  AudioDeviceModel model;
  model.applyAdd(makeSource(1, "source1", 40));
  QSignalSpy rows_inserted(&model, &AudioDeviceModel::rowsInserted);
  QSignalSpy data_changed(&model, &AudioDeviceModel::dataChanged);

  model.applyAdd(makeSource(1, "source1", 70, true, true));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 0);
  EXPECT_EQ(data_changed.count(), 1);
  const QList<int> changed_roles =
      data_changed.first().at(2).value<QList<int>>();
  EXPECT_EQ(changed_roles,
            (QList<int>{static_cast<int>(AudioDeviceModel::Role::Volume),
                        static_cast<int>(AudioDeviceModel::Role::Muted),
                        static_cast<int>(AudioDeviceModel::Role::IsDefault)}));
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::Volume))
                .toUInt(),
            70U);
  EXPECT_TRUE(model.data(item, static_cast<int>(AudioDeviceModel::Role::Muted))
                  .toBool());
  EXPECT_TRUE(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::IsDefault))
          .toBool());
}

TEST(AudioDeviceModel, DataReturnsAllRoles) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(42, "alsa_output", 75, true, true,
                          QStringLiteral("Analog"), 2, 48000, QString(),
                          QStringLiteral("audio-speakers")));

  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::DeviceId))
                .toUInt(),
            42U);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::Name))
                .toString(),
            QStringLiteral("alsa_output"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::DisplayName))
          .toString(),
      QStringLiteral("Test display name"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::RawDescription))
          .toString(),
      QStringLiteral("Test description"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::VendorName))
          .toString(),
      QStringLiteral("Test vendor"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::ProductName))
          .toString(),
      QStringLiteral("Test product"));
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::PortName))
                .toString(),
            QStringLiteral("analog-output-speaker"));
  EXPECT_EQ(
      model
          .data(item, static_cast<int>(AudioDeviceModel::Role::PortDescription))
          .toString(),
      QStringLiteral("Speakers"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::FormFactor))
          .toString(),
      QStringLiteral("internal"));
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::Volume))
                .toUInt(),
            75U);
  EXPECT_TRUE(model.data(item, static_cast<int>(AudioDeviceModel::Role::Muted))
                  .toBool());
  EXPECT_TRUE(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::IsDefault))
          .toBool());
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::BusType))
                .toString(),
            QStringLiteral("Analog"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::ChannelCount))
          .toUInt(),
      2U);
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioDeviceModel::Role::SampleRate))
          .toUInt(),
      48000U);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::Codec))
                .toString(),
            QString());
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::IconName))
                .toString(),
            QStringLiteral("audio-speakers"));
}

TEST(AudioDeviceModel, InvalidIndexReturnsNullVariant) {
  AudioDeviceModel model;
  EXPECT_FALSE(model.data(model.index(0)).isValid());
  EXPECT_FALSE(model.data(QModelIndex()).isValid());
}

TEST(AudioDeviceModel, ApplyChangeEmitsDataChanged) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "sink1", 50));
  QSignalSpy data_changed(&model, &AudioDeviceModel::dataChanged);

  model.applyChange(makeSink(1, "sink1", 80));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(data_changed.count(), 1);
  const QList<int> changed_roles =
      data_changed.first().at(2).value<QList<int>>();
  EXPECT_EQ(changed_roles,
            QList<int>{static_cast<int>(AudioDeviceModel::Role::Volume)});
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::Volume))
                .toUInt(),
            80U);
}

TEST(AudioDeviceModel, ApplyChangeUpsertIfNotFound) {
  AudioDeviceModel model;
  QSignalSpy rows_inserted(&model, &AudioDeviceModel::rowsInserted);

  model.applyChange(makeSink(99, "new_sink"));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 1);
}

TEST(AudioDeviceModel, ApplyRemoveByDeviceId) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1"));
  model.applyAdd(makeSink(2, "s2"));
  QSignalSpy rows_removed(&model, &AudioDeviceModel::rowsRemoved);

  model.applyRemove(1);

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_removed.count(), 1);
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioDeviceModel::Role::DeviceId))
                .toUInt(),
            2U);
}

TEST(AudioDeviceModel, ApplyRemoveIgnoresMissingId) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1"));
  QSignalSpy rows_removed(&model, &AudioDeviceModel::rowsRemoved);

  model.applyRemove(999);

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_removed.count(), 0);
}

// T-014: locks AudioDeviceModel::applyRemove() to the same shape as
// AudioStreamModel::applyRemove() — linear scan from the front, remove the
// first id match, silent no-op when absent. See the mirrored test in
// test_audio_stream_model.cpp.
TEST(AudioDeviceModel, ApplyRemoveMiddleElementPreservesOrderOfRemaining) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1"));
  model.applyAdd(makeSink(2, "s2"));
  model.applyAdd(makeSink(3, "s3"));

  model.applyRemove(2);

  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(model
                .data(model.index(0),
                      static_cast<int>(AudioDeviceModel::Role::DeviceId))
                .toUInt(),
            1U);
  EXPECT_EQ(model
                .data(model.index(1),
                      static_cast<int>(AudioDeviceModel::Role::DeviceId))
                .toUInt(),
            3U);
}

TEST(AudioDeviceModel, ClearEmitsModelReset) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1"));
  model.applyAdd(makeSink(2, "s2"));
  QSignalSpy model_reset(&model, &AudioDeviceModel::modelReset);

  model.clear();

  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model_reset.count(), 1);
}

TEST(AudioDeviceModel, RoleNamesAreCorrect) {
  AudioDeviceModel model;
  const QHash<int, QByteArray> roles = model.roleNames();

  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::DeviceId)),
            "deviceId");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::Name)),
            "name");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::Description)),
            "description");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::DisplayName)),
            "displayName");
  EXPECT_EQ(
      roles.value(static_cast<int>(AudioDeviceModel::Role::RawDescription)),
      "rawDescription");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::VendorName)),
            "vendorName");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::ProductName)),
            "productName");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::PortName)),
            "portName");
  EXPECT_EQ(
      roles.value(static_cast<int>(AudioDeviceModel::Role::PortDescription)),
      "portDescription");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::FormFactor)),
            "formFactor");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::Volume)),
            "volume");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::Muted)),
            "muted");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::IsDefault)),
            "isDefault");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::BusType)),
            "busType");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::ChannelCount)),
            "channelCount");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::SampleRate)),
            "sampleRate");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::Codec)),
            "codec");
  EXPECT_EQ(roles.value(static_cast<int>(AudioDeviceModel::Role::IconName)),
            "iconName");
}

TEST(AudioDeviceModel, DefaultDeviceStartsEmpty) {
  AudioDeviceModel model;
  EXPECT_TRUE(model.defaultDevice().isEmpty());
}

TEST(AudioDeviceModel, DefaultDevicePopulatesWhenDefaultDeviceAdded) {
  AudioDeviceModel model;
  QSignalSpy default_device_changed(&model,
                                    &AudioDeviceModel::defaultDeviceChanged);

  model.applyAdd(makeSink(1, "s1", 50, false, false));
  EXPECT_EQ(default_device_changed.count(), 0);
  EXPECT_TRUE(model.defaultDevice().isEmpty());

  model.applyAdd(makeSink(2, "s2", 60, false, true));

  EXPECT_EQ(default_device_changed.count(), 1);
  const QVariantMap default_device = model.defaultDevice();
  EXPECT_EQ(default_device.value("deviceId").toUInt(), 2U);
  EXPECT_EQ(default_device.value("name").toString(), QStringLiteral("s2"));
  EXPECT_EQ(default_device.value("displayName").toString(),
            QStringLiteral("Test display name"));
  EXPECT_EQ(default_device.value("rawDescription").toString(),
            QStringLiteral("Test description"));
  EXPECT_TRUE(default_device.value("isDefault").toBool());
}

TEST(AudioDeviceModel,
     DefaultDeviceReemitsWhenDefaultRowChangesWhileRemainingDefault) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1", 50, false, true));
  QSignalSpy default_device_changed(&model,
                                    &AudioDeviceModel::defaultDeviceChanged);

  model.applyChange(makeSink(1, "s1", 90, true, true));

  EXPECT_EQ(default_device_changed.count(), 1);
  EXPECT_EQ(model.defaultDevice().value("volume").toUInt(), 90U);
  EXPECT_TRUE(model.defaultDevice().value("muted").toBool());
}

TEST(AudioDeviceModel,
     DefaultDeviceDoesNotReemitOnUnrelatedNonDefaultRowChange) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1", 50, false, true));
  model.applyAdd(makeSink(2, "s2", 20, false, false));
  QSignalSpy default_device_changed(&model,
                                    &AudioDeviceModel::defaultDeviceChanged);

  model.applyChange(makeSink(2, "s2", 99, true, false));

  EXPECT_EQ(default_device_changed.count(), 0);
  EXPECT_EQ(model.defaultDevice().value("deviceId").toUInt(), 1U);
}

TEST(AudioDeviceModel, DefaultDeviceGoesEmptyWhenDefaultDeviceRemoved) {
  AudioDeviceModel model;
  model.applyAdd(makeSink(1, "s1", 50, false, true));
  QSignalSpy default_device_changed(&model,
                                    &AudioDeviceModel::defaultDeviceChanged);

  model.applyRemove(1);

  EXPECT_EQ(default_device_changed.count(), 1);
  EXPECT_TRUE(model.defaultDevice().isEmpty());
}
