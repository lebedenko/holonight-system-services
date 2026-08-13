#include "AudioStreamModel.h"

using namespace HoloNight::System;

#include <QSignalSpy>

#include <gtest/gtest.h>

namespace {

AudioStream makeStream(uint32_t stream_id, const char *name,
                       AudioStreamType type = AudioStreamType::SinkInput,
                       uint32_t device = 0, uint8_t volume = 60) {
  AudioStream stream;
  stream.id = stream_id;
  stream.name = QString::fromUtf8(name);
  stream.application = QStringLiteral("TestApp");
  stream.device = device;
  stream.volume = volume;
  stream.muted = false;
  stream.type = type;
  return stream;
}

} // namespace

TEST(AudioStreamModel, RowCountIsZeroInitially) {
  AudioStreamModel model;
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(AudioStreamModel, ApplyAddIncreasesRowCount) {
  AudioStreamModel model;
  QSignalSpy rows_inserted(&model, &AudioStreamModel::rowsInserted);

  model.applyAdd(makeStream(1, "music"));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 1);
}

TEST(AudioStreamModel, ApplyAddUpdatesExistingRowByStreamId) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "music", AudioStreamType::SinkInput, 0, 40));
  QSignalSpy rows_inserted(&model, &AudioStreamModel::rowsInserted);
  QSignalSpy data_changed(&model, &AudioStreamModel::dataChanged);

  model.applyAdd(makeStream(1, "music", AudioStreamType::SinkInput, 0, 75));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 0);
  EXPECT_EQ(data_changed.count(), 1);
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::Volume))
                .toUInt(),
            75U);
}

TEST(AudioStreamModel, DataReturnsAllRoles) {
  AudioStreamModel model;
  AudioStream stream =
      makeStream(7, "video", AudioStreamType::SourceOutput, 3, 88);
  stream.application = QStringLiteral("Firefox");
  stream.icon_name = QStringLiteral("firefox");
  stream.muted = true;
  model.applyAdd(stream);

  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::StreamId))
                .toUInt(),
            7U);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::Name))
                .toString(),
            QStringLiteral("video"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioStreamModel::Role::Application))
          .toString(),
      QStringLiteral("Firefox"));
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::IconName))
                .toString(),
            QStringLiteral("firefox"));
  EXPECT_EQ(
      model.data(item, static_cast<int>(AudioStreamModel::Role::CurrentDevice))
          .toUInt(),
      3U);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::Volume))
                .toUInt(),
            88U);
  EXPECT_TRUE(model.data(item, static_cast<int>(AudioStreamModel::Role::Muted))
                  .toBool());
}

TEST(AudioStreamModel, InvalidIndexReturnsNullVariant) {
  AudioStreamModel model;
  EXPECT_FALSE(model.data(model.index(0)).isValid());
  EXPECT_FALSE(model.data(QModelIndex()).isValid());
}

TEST(AudioStreamModel, ApplyChangeUpdatesExistingRow) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "music", AudioStreamType::SinkInput, 0, 50));
  QSignalSpy data_changed(&model, &AudioStreamModel::dataChanged);

  model.applyChange(makeStream(1, "music", AudioStreamType::SinkInput, 0, 90));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(data_changed.count(), 1);
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::Volume))
                .toUInt(),
            90U);
}

TEST(AudioStreamModel, ApplyChangeUpsertIfNotFound) {
  AudioStreamModel model;
  QSignalSpy rows_inserted(&model, &AudioStreamModel::rowsInserted);

  model.applyChange(makeStream(42, "new"));

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_inserted.count(), 1);
}

TEST(AudioStreamModel, ApplyRemoveByStreamId) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "a"));
  model.applyAdd(makeStream(2, "b"));
  QSignalSpy rows_removed(&model, &AudioStreamModel::rowsRemoved);

  model.applyRemove(1);

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_removed.count(), 1);
  const QModelIndex item = model.index(0);
  EXPECT_EQ(model.data(item, static_cast<int>(AudioStreamModel::Role::StreamId))
                .toUInt(),
            2U);
}

TEST(AudioStreamModel, ApplyRemoveIgnoresMissingId) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "a"));
  QSignalSpy rows_removed(&model, &AudioStreamModel::rowsRemoved);

  model.applyRemove(999);

  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(rows_removed.count(), 0);
}

// T-014: locks AudioStreamModel::applyRemove() to the same shape as
// AudioDeviceModel::applyRemove() — linear scan from the front, remove the
// first id match, silent no-op when absent. See the mirrored test in
// test_audio_device_model.cpp.
TEST(AudioStreamModel, ApplyRemoveMiddleElementPreservesOrderOfRemaining) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "a"));
  model.applyAdd(makeStream(2, "b"));
  model.applyAdd(makeStream(3, "c"));

  model.applyRemove(2);

  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(model
                .data(model.index(0),
                      static_cast<int>(AudioStreamModel::Role::StreamId))
                .toUInt(),
            1U);
  EXPECT_EQ(model
                .data(model.index(1),
                      static_cast<int>(AudioStreamModel::Role::StreamId))
                .toUInt(),
            3U);
}

TEST(AudioStreamModel, ClearEmitsModelReset) {
  AudioStreamModel model;
  model.applyAdd(makeStream(1, "a"));
  model.applyAdd(makeStream(2, "b"));
  QSignalSpy model_reset(&model, &AudioStreamModel::modelReset);

  model.clear();

  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model_reset.count(), 1);
}

TEST(AudioStreamModel, RoleNamesAreCorrect) {
  AudioStreamModel model;
  const QHash<int, QByteArray> roles = model.roleNames();

  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::StreamId)),
            "streamId");
  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::Name)),
            "name");
  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::Application)),
            "application");
  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::IconName)),
            "iconName");
  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::Volume)),
            "volume");
  EXPECT_EQ(roles.value(static_cast<int>(AudioStreamModel::Role::Muted)),
            "muted");
  EXPECT_EQ(
      roles.value(static_cast<int>(AudioStreamModel::Role::CurrentDevice)),
      "currentDevice");
}
