#include "StorageController.h"
#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace HoloNight::System;
namespace {
class Backend : public StorageBackend {
public:
  QList<StorageResult> calls;
  QList<StorageDrive> drives;
  QList<StorageVolume> volumes;
  void start() override {}
  void stop() override {}
  void execute(const QString &id, StorageOperation op,
               const QString &target) override {
    calls.append({id, target, op, {}, {}, {}});
  }
  void publish() { emit snapshotChanged(drives, volumes, true); }
  void complete(QString failure = {}, QString path = {}) {
    auto result = calls.last();
    result.errorName = failure;
    result.mountPath = path;
    if (failure.isEmpty() && result.operation == StorageOperation::Unmount)
      for (auto &v : volumes)
        if (v.id == result.targetId) {
          v.mountPoints.clear();
          v.canUnmount = false;
          v.canMount = true;
        }
    publish();
    emit operationFinished(result);
  }
};
StorageDrive drive(QString id, QString sibling = {}) {
  StorageDrive d;
  d.id = id;
  d.siblingId = sibling;
  d.removable = true;
  d.mediaPresent = true;
  d.canEject = true;
  d.canPowerOff = true;
  return d;
}
StorageVolume volume(QString id, QString driveId, bool mounted = true) {
  StorageVolume v;
  v.id = id;
  v.driveId = driveId;
  v.canMount = !mounted;
  v.canUnmount = mounted;
  if (mounted)
    v.mountPoints = {QStringLiteral("/mnt/") + id};
  return v;
}
void dispatch() { QCoreApplication::processEvents(); }
StorageResult result(const QSignalSpy &spy, int index = 0) {
  return qvariant_cast<StorageResult>(spy.at(index).at(0));
}
} // namespace
TEST(StorageController,
     MountReturnsCorrelatedPathAndDoesNotOptimisticallyChangeState) {
  Backend b;
  StorageController c(&b);
  b.volumes = {volume("v", {}, false)};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  const auto id = c.mount("v");
  EXPECT_TRUE(c.busy("v"));
  EXPECT_EQ(spy.size(), 0);
  dispatch();
  ASSERT_EQ(b.calls.size(), 1);
  EXPECT_TRUE(c.volumes()->find("v")->mountPoints.isEmpty());
  b.complete({}, "/media/data");
  ASSERT_EQ(spy.size(), 1);
  EXPECT_EQ(result(spy).requestId, id);
  EXPECT_EQ(result(spy).mountPath, "/media/data");
  EXPECT_FALSE(c.busy("v"));
}
TEST(StorageController, RemovalUnmountsHiddenVolumesAndStopsAtFirstFailure) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("d")};
  b.volumes = {volume("a", "d"), volume("hidden", "d")};
  b.volumes[1].hintIgnore = true;
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.eject("d");
  dispatch();
  ASSERT_EQ(b.calls.size(), 1);
  EXPECT_EQ(b.calls[0].targetId, "a");
  b.complete();
  ASSERT_EQ(b.calls.size(), 2);
  EXPECT_EQ(b.calls[1].targetId, "hidden");
  b.complete("org.freedesktop.UDisks2.Error.DeviceBusy");
  ASSERT_EQ(spy.size(), 1);
  EXPECT_FALSE(result(spy).succeeded());
  EXPECT_EQ(b.calls.size(), 2);
}
TEST(StorageController,
     PowerOffRequiresExactSiblingScopeAndUnmountsAllBeforePowerOff) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("a", "usb"), drive("b", "usb")};
  b.volumes = {volume("v1", "a"), volume("v2", "b")};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.powerOff("a", {"a", "v1"});
  dispatch();
  EXPECT_TRUE(b.calls.isEmpty());
  EXPECT_EQ(result(spy).errorName, "org.holonight.Storage.ScopeChanged");
  const auto scope = c.removalScope("a", true);
  EXPECT_EQ(scope.size(), 4);
  c.powerOff("a", scope);
  dispatch();
  EXPECT_TRUE(c.busy("b"));
  b.complete();
  b.complete();
  ASSERT_EQ(b.calls.size(), 3);
  EXPECT_EQ(b.calls.last().operation, StorageOperation::PowerOff);
  b.complete();
  EXPECT_FALSE(c.busy("b"));
}
TEST(StorageController, NewSiblingDuringUnmountInvalidatesConfirmation) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("a", "usb")};
  b.volumes = {volume("v", "a")};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.powerOff("a", c.removalScope("a", true));
  dispatch();
  b.drives.append(drive("new", "usb"));
  b.complete();
  ASSERT_EQ(spy.size(), 1);
  EXPECT_EQ(result(spy).errorName, "org.holonight.Storage.ScopeChanged");
  EXPECT_EQ(b.calls.size(), 1);
}
TEST(StorageController, ConflictsAreRejectedButIndependentDrivesCanProceed) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("a"), drive("b")};
  b.volumes = {volume("v1", "a", false), volume("v2", "b", false)};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount("v1");
  c.eject("a");
  c.mount("v2");
  dispatch();
  EXPECT_EQ(b.calls.size(), 2);
  ASSERT_EQ(spy.size(), 1);
  EXPECT_EQ(result(spy).errorName, "org.holonight.Storage.Busy");
}
TEST(StorageController, DisconnectRetiresRequestsAndIgnoresLateReplies) {
  Backend b;
  StorageController c(&b);
  b.volumes = {volume("old", {}, false)};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount("old");
  dispatch();
  emit b.snapshotChanged({}, {}, false);
  EXPECT_FALSE(c.available());
  EXPECT_EQ(c.volumes()->rowCount(), 0);
  EXPECT_EQ(spy.size(), 1);
  b.complete();
  EXPECT_EQ(spy.size(), 1);
  EXPECT_FALSE(c.busy("old"));
}
TEST(StorageController, AuthorizationCancellationIsReturnedWithoutRetry) {
  Backend b;
  StorageController c(&b);
  b.volumes = {volume("v", {}, false)};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount("v");
  dispatch();
  b.complete("org.freedesktop.UDisks2.Error.NotAuthorizedDismissed");
  ASSERT_EQ(spy.size(), 1);
  EXPECT_EQ(result(spy).errorName,
            "org.freedesktop.UDisks2.Error.NotAuthorizedDismissed");
  dispatch();
  EXPECT_EQ(b.calls.size(), 1);
}
TEST(StorageModels, UpdatePreservesIdentityAndRemovalInvalidatesIndex) {
  StorageVolumeModel model;
  QAbstractItemModelTester tester(
      &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  auto v = volume("opaque", {}, false);
  model.apply({v});
  QPersistentModelIndex index(model.index(0));
  v.label = "Renamed";
  model.apply({v});
  EXPECT_TRUE(index.isValid());
  EXPECT_EQ(model.data(index, StorageVolumeModel::LabelRole).toString(),
            "Renamed");
  model.apply({});
  EXPECT_FALSE(index.isValid());
  EXPECT_FALSE(model.find("opaque"));
}
TEST(StorageController, ExternalRemountStopsFinalDriveRemoval) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("d")};
  b.volumes = {volume("a", "d"), volume("b", "d")};
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.eject("d");
  dispatch();
  b.complete();
  b.volumes[0].mountPoints = {"/mnt/a"};
  b.volumes[0].canUnmount = true;
  b.complete();
  ASSERT_EQ(spy.size(), 1);
  EXPECT_EQ(result(spy).errorName, "org.holonight.Storage.Busy");
  EXPECT_EQ(b.calls.size(), 2);
}
TEST(StorageController, MissingTargetsAndChangedCapabilitiesNeverReachBackend) {
  Backend b;
  StorageController c(&b);
  b.drives = {drive("d")};
  b.drives[0].canEject = false;
  b.publish();
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount("missing");
  c.eject("d");
  dispatch();
  EXPECT_TRUE(b.calls.isEmpty());
  ASSERT_EQ(spy.size(), 2);
  EXPECT_EQ(result(spy).errorName, "org.holonight.Storage.Disappeared");
  EXPECT_EQ(result(spy, 1).errorName, "org.holonight.Storage.NotSupported");
}
TEST(StorageModels, DriveModelRetainsEmptyReadersAndAllRawFacts) {
  StorageDriveModel model;
  QAbstractItemModelTester tester(
      &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  auto reader = drive("reader");
  reader.mediaPresent = false;
  model.apply({reader});
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_FALSE(
      model.data(model.index(0), StorageDriveModel::MediaPresentRole).toBool());
  reader.mediaPresent = true;
  model.apply({reader});
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_TRUE(
      model.data(model.index(0), StorageDriveModel::MediaPresentRole).toBool());
}
