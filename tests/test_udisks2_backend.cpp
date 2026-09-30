#include "StorageController.h"
#include "UDisks2Backend.h"
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

using namespace HoloNight::System;
namespace {
const QString service = QStringLiteral("org.holonight.TestUDisks");
const QString root = QStringLiteral("/org/freedesktop/UDisks2");
const QString drivePath = root + QStringLiteral("/drives/usb");
const QString volumePath = root + QStringLiteral("/block_devices/sdb1");
const QString driveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString blockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString partitionInterface =
    QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString fsInterface =
    QStringLiteral("org.freedesktop.UDisks2.Filesystem");
class FakeUDisks : public QDBusVirtualObject {
public:
  StorageObjects objects;
  QList<QDBusMessage> pendingSnapshots;
  QList<QDBusMessage> pendingOperations;
  QList<QString> methods;
  bool holdSnapshots = false;
  bool holdOperations = false;
  QString operationError;
  QDBusConnection bus;
  explicit FakeUDisks(QDBusConnection connection) : bus(std::move(connection)) {
    objects[QDBusObjectPath(drivePath)][driveInterface] = {
        {"Vendor", "Test"},       {"Model", "External SSD"},
        {"Removable", true},      {"MediaRemovable", false},
        {"MediaAvailable", true}, {"CanPowerOff", true},
        {"Ejectable", false},     {"SiblingId", "usb"}};
    objects[QDBusObjectPath(volumePath)][blockInterface] = {
        {"Drive", QVariant::fromValue(QDBusObjectPath(drivePath))},
        {"IdUsage", "filesystem"},
        {"IdType", "ext4"},
        {"IdLabel", "Data"},
        {"Size", QVariant::fromValue(quint64(4000000000ULL))},
        {"HintSystem", true},
        {"HintIgnore", false},
        {"PreferredDevice", QByteArray("/dev/sdb1\0", 10)}};
    objects[QDBusObjectPath(volumePath)][fsInterface] = {
        {"MountPoints", QVariant::fromValue(QList<QByteArray>{})}};
  }
  QString introspect(const QString &) const override { return {}; }
  bool handleMessage(const QDBusMessage &message,
                     const QDBusConnection &) override {
    if (message.member() == QLatin1String("GetManagedObjects")) {
      if (holdSnapshots)
        pendingSnapshots.append(message);
      else
        replySnapshot(message, objects);
      return true;
    }
    if (message.interface() == fsInterface ||
        message.interface() == driveInterface) {
      EXPECT_TRUE(message.isInteractiveAuthorizationAllowed());
      EXPECT_EQ(message.arguments().size(), 1);
      EXPECT_TRUE(
          qdbus_cast<QVariantMap>(message.arguments().first()).isEmpty());
      methods.append(message.member());
      if (holdOperations)
        pendingOperations.append(message);
      else
        replyOperation(message);
      return true;
    }
    return false;
  }
  void replySnapshot(const QDBusMessage &message,
                     const StorageObjects &snapshot) {
    bus.send(message.createReply({QVariant::fromValue(snapshot)}));
  }
  void replyOperation(const QDBusMessage &message) {
    if (!operationError.isEmpty()) {
      bus.send(message.createErrorReply(operationError, "Test failure"));
      return;
    }
    if (message.member() == QLatin1String("Mount")) {
      objects[QDBusObjectPath(message.path())][fsInterface]["MountPoints"] =
          QVariant::fromValue(
              QList<QByteArray>{QByteArray("/media/Data\0", 12)});
      bus.send(
          message.createReply(QVariantList{QStringLiteral("/media/Data")}));
    } else {
      if (message.member() == QLatin1String("Unmount"))
        objects[QDBusObjectPath(message.path())][fsInterface]["MountPoints"] =
            QVariant::fromValue(QList<QByteArray>{});
      bus.send(message.createReply());
    }
  }
  void changed(QStringList invalidated = {}) {
    auto signal = QDBusMessage::createSignal(
        volumePath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << blockInterface << QVariantMap{} << invalidated;
    bus.send(signal);
  }
  void removeVolume() {
    objects.remove(QDBusObjectPath(volumePath));
    auto signal = QDBusMessage::createSignal(
        root, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
    signal << QVariant::fromValue(QDBusObjectPath(volumePath))
           << QStringList{blockInterface, fsInterface};
    bus.send(signal);
  }
  void addVolume(const StorageInterfaces &interfaces) {
    objects[QDBusObjectPath(volumePath)] = interfaces;
    auto signal = QDBusMessage::createSignal(
        root, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
    signal << QVariant::fromValue(QDBusObjectPath(volumePath))
           << QVariant::fromValue(interfaces);
    bus.send(signal);
  }
};
class UDisksTest : public testing::Test {
protected:
  QDBusConnection bus = QDBusConnection::connectToBus(
      QDBusConnection::SessionBus, "storage-test-service");
  FakeUDisks fake{bus};
  void SetUp() override {
    qDBusRegisterMetaType<StorageObjects>();
    qDBusRegisterMetaType<StorageInterfaces>();
    qDBusRegisterMetaType<QList<QByteArray>>();
    ASSERT_TRUE(bus.isConnected());
    ASSERT_TRUE(bus.registerService(service));
    ASSERT_TRUE(
        bus.registerVirtualObject(root, &fake, QDBusConnection::SubPath));
  }
  void TearDown() override {
    bus.unregisterObject(root, QDBusConnection::UnregisterTree);
    bus.unregisterService(service);
  }
};
} // namespace
TEST_F(UDisksTest, DiscoveryInvalidationHotplugAndIdentity) {
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  ASSERT_EQ(c.drives()->rowCount(), 1);
  ASSERT_EQ(c.volumes()->rowCount(), 1);
  const auto d = c.drives()->items().first();
  const auto v = c.volumes()->items().first();
  EXPECT_TRUE(d.removable);
  EXPECT_FALSE(d.mediaRemovable);
  EXPECT_FALSE(d.canEject);
  EXPECT_TRUE(d.canPowerOff);
  EXPECT_EQ(v.driveId, d.id);
  EXPECT_TRUE(v.hintSystem);
  EXPECT_EQ(v.capacity, 4000000000ULL);
  fake.objects[QDBusObjectPath(volumePath)][blockInterface]["IdLabel"] =
      "Changed";
  fake.changed({"IdLabel"});
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return c.volumes()->items().first().label == "Changed"; }));
  EXPECT_EQ(c.volumes()->items().first().id, v.id);
  auto interfaces = fake.objects.value(QDBusObjectPath(volumePath));
  fake.removeVolume();
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.volumes()->rowCount() == 0; }));
  fake.addVolume(interfaces);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.volumes()->rowCount() == 1; }));
  EXPECT_NE(c.volumes()->items().first().id, v.id);
}
TEST_F(UDisksTest, PresentationMetadataTracksUDisksSnapshots) {
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto initial = c.volumes()->items().first();
  EXPECT_EQ(initial.label, "Data");
  EXPECT_TRUE(initial.hintName.isEmpty());
  EXPECT_TRUE(initial.hintIconName.isEmpty());
  EXPECT_TRUE(initial.hintSymbolicIconName.isEmpty());
  EXPECT_TRUE(initial.partitionName.isEmpty());
  EXPECT_EQ(initial.partitionNumber, 0U);
  EXPECT_TRUE(c.drives()->items().first().media.isEmpty());
  EXPECT_FALSE(c.drives()->items().first().rotationRate);
  EXPECT_FALSE(c.drives()->index(0).data(StorageDriveModel::RotationRateRole).isValid());
  EXPECT_TRUE(c.drives()->items().first().mediaCompatibility.isEmpty());

  auto &block = fake.objects[QDBusObjectPath(volumePath)][blockInterface];
  block["HintName"] = "Archive";
  block["HintIconName"] = "drive-removable-media";
  block["HintSymbolicIconName"] = "drive-removable-media-symbolic";
  fake.objects[QDBusObjectPath(volumePath)][partitionInterface] = {
      {"Name", "Photos"}, {"Number", 1U}};
  auto &drive = fake.objects[QDBusObjectPath(drivePath)][driveInterface];
  drive["Media"] = "thumb";
  drive["MediaCompatibility"] = QStringList{"thumb", "flash"};
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return c.volumes()->items().first().hintName == "Archive" &&
           c.drives()->items().first().media == "thumb";
  }));
  const auto volume = c.volumes()->items().first();
  const auto media = c.drives()->items().first();
  EXPECT_EQ(volume.id, initial.id);
  EXPECT_EQ(volume.label, "Data");
  EXPECT_EQ(volume.hintIconName, "drive-removable-media");
  EXPECT_EQ(volume.hintSymbolicIconName, "drive-removable-media-symbolic");
  EXPECT_EQ(volume.partitionName, "Photos");
  EXPECT_EQ(volume.partitionNumber, 1U);
  EXPECT_EQ(media.mediaCompatibility, (QStringList{"thumb", "flash"}));

  const auto volumeIndex = c.volumes()->index(0);
  const auto driveIndex = c.drives()->index(0);
  const auto volumeRoles = c.volumes()->roleNames();
  const auto driveRoles = c.drives()->roleNames();
  EXPECT_EQ(volumeRoles.value(StorageVolumeModel::HintNameRole), "hintName");
  EXPECT_EQ(volumeRoles.value(StorageVolumeModel::HintIconNameRole),
            "hintIconName");
  EXPECT_EQ(volumeRoles.value(StorageVolumeModel::HintSymbolicIconNameRole),
            "hintSymbolicIconName");
  EXPECT_EQ(volumeRoles.value(StorageVolumeModel::PartitionNameRole),
            "partitionName");
  EXPECT_EQ(volumeRoles.value(StorageVolumeModel::PartitionNumberRole),
            "partitionNumber");
  EXPECT_EQ(driveRoles.value(StorageDriveModel::MediaRole), "media");
  EXPECT_EQ(driveRoles.value(StorageDriveModel::MediaCompatibilityRole),
            "mediaCompatibility");
  EXPECT_EQ(c.volumes()
                ->data(volumeIndex, StorageVolumeModel::HintNameRole)
                .toString(),
            "Archive");
  EXPECT_EQ(c.volumes()
                ->data(volumeIndex, StorageVolumeModel::HintIconNameRole)
                .toString(),
            "drive-removable-media");
  EXPECT_EQ(
      c.volumes()
          ->data(volumeIndex, StorageVolumeModel::HintSymbolicIconNameRole)
          .toString(),
      "drive-removable-media-symbolic");
  EXPECT_EQ(c.volumes()
                ->data(volumeIndex, StorageVolumeModel::PartitionNameRole)
                .toString(),
            "Photos");
  EXPECT_EQ(c.volumes()
                ->data(volumeIndex, StorageVolumeModel::PartitionNumberRole)
                .toUInt(),
            1U);
  EXPECT_EQ(
      c.drives()->data(driveIndex, StorageDriveModel::MediaRole).toString(),
      "thumb");
  EXPECT_EQ(c.drives()
                ->data(driveIndex, StorageDriveModel::MediaCompatibilityRole)
                .toStringList(),
            (QStringList{"thumb", "flash"}));

  block.remove("HintName");
  block.remove("HintIconName");
  block.remove("HintSymbolicIconName");
  fake.objects[QDBusObjectPath(volumePath)].remove(partitionInterface);
  drive.remove("Media");
  drive.remove("MediaCompatibility");
  fake.changed({"HintName", "HintIconName", "HintSymbolicIconName"});
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return c.volumes()->items().first().hintName.isEmpty() &&
           c.drives()->items().first().media.isEmpty();
  }));
  EXPECT_EQ(c.volumes()->items().first(), initial);
  EXPECT_TRUE(c.drives()->items().first().mediaCompatibility.isEmpty());

  block["IdLabel"] = "";
  fake.changed({"IdLabel"});
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return c.volumes()->items().first().label.isEmpty(); }));
  EXPECT_TRUE(c.volumes()->items().first().hintName.isEmpty());
  EXPECT_TRUE(c.volumes()->items().first().partitionName.isEmpty());
  EXPECT_EQ(c.volumes()->items().first().device, "/dev/sdb1");
}
TEST_F(UDisksTest, InitialEnumerationRaceDoesNotResurrectRemovedDevice) {
  fake.holdSnapshots = true;
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !fake.pendingSnapshots.isEmpty(); }));
  const auto stale = fake.objects;
  fake.removeVolume();
  fake.replySnapshot(fake.pendingSnapshots.takeFirst(), stale);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !fake.pendingSnapshots.isEmpty(); }));
  EXPECT_FALSE(c.available());
  fake.holdSnapshots = false;
  fake.replySnapshot(fake.pendingSnapshots.takeFirst(), fake.objects);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  EXPECT_EQ(c.volumes()->rowCount(), 0);
}
TEST_F(UDisksTest, RestartChangesIdsAndRetiresLateOperationReply) {
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto oldId = c.volumes()->items().first().id;
  fake.holdOperations = true;
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount(oldId);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !fake.pendingOperations.isEmpty(); }));
  bus.unregisterService(service);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !c.available(); }));
  ASSERT_EQ(spy.size(), 1);
  EXPECT_FALSE(qvariant_cast<StorageResult>(spy.first().first()).succeeded());
  ASSERT_TRUE(bus.registerService(service));
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  EXPECT_NE(c.volumes()->items().first().id, oldId);
  fake.replyOperation(fake.pendingOperations.takeFirst());
  QCoreApplication::processEvents();
  EXPECT_EQ(spy.size(), 1);
}
TEST_F(UDisksTest, MountRefreshesModelBeforeCompletionAndPropagatesBusy) {
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto id = c.volumes()->items().first().id;
  QSignalSpy spy(&c, &StorageController::operationFinished);
  c.mount(id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.size() == 1; }));
  EXPECT_EQ(qvariant_cast<StorageResult>(spy.first().first()).mountPath,
            "/media/Data");
  EXPECT_EQ(c.volumes()->find(id)->mountPoints, QStringList{"/media/Data"});
  fake.operationError = "org.freedesktop.UDisks2.Error.DeviceBusy";
  c.unmount(id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.size() == 2; }));
  EXPECT_EQ(qvariant_cast<StorageResult>(spy.last().first()).errorName,
            fake.operationError);
  EXPECT_TRUE(c.volumes()->find(id)->canUnmount);
}
TEST_F(UDisksTest,
       IndependentControllersSeeUpstreamMountAndConflictingFailure) {
  UDisks2Backend a(QDBusConnection::sessionBus(), service),
      b(QDBusConnection::sessionBus(), service);
  StorageController first(&a), second(&b);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return first.available() && second.available(); }));
  QSignalSpy completed(&first, &StorageController::operationFinished);
  first.mount(first.volumes()->items().first().id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return completed.size() == 1; }));
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return second.volumes()->items().first().canUnmount; }));
  EXPECT_FALSE(second.busy(second.volumes()->items().first().id));
}
TEST_F(UDisksTest, SnapshotAfterRestartCannotRestoreOldIdsOrObjects) {
  fake.holdSnapshots = true;
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return fake.pendingSnapshots.size() == 1; }));
  const auto oldMessage = fake.pendingSnapshots.takeFirst();
  const auto oldObjects = fake.objects;
  bus.unregisterService(service);
  // Observe the owner's loss through a backend snapshot before registering
  // again.
  QSignalSpy snapshots(&backend, &StorageBackend::snapshotChanged);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !snapshots.isEmpty(); }));
  fake.objects.clear();
  ASSERT_TRUE(bus.registerService(service));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return fake.pendingSnapshots.size() == 1; }));
  fake.replySnapshot(fake.pendingSnapshots.takeFirst(), fake.objects);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  fake.replySnapshot(oldMessage, oldObjects);
  fake.changed();
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return fake.pendingSnapshots.size() == 1; }));
  EXPECT_EQ(c.volumes()->rowCount(), 0);
  EXPECT_EQ(c.drives()->rowCount(), 0);
  fake.replySnapshot(fake.pendingSnapshots.takeFirst(), fake.objects);
}
TEST_F(UDisksTest, LockedAndUnlockedStorageKeepFullBackingTopology) {
  const auto clearPath = root + "/block_devices/dm_0";
  const QString encrypted = "org.freedesktop.UDisks2.Encrypted";
  fake.objects[QDBusObjectPath(volumePath)].remove(fsInterface);
  fake.objects[QDBusObjectPath(volumePath)][blockInterface]["IdUsage"] =
      "crypto";
  fake.objects[QDBusObjectPath(volumePath)][encrypted] = {
      {"CleartextDevice", QVariant::fromValue(QDBusObjectPath("/"))}};
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  auto locked = c.volumes()->items().first();
  EXPECT_TRUE(locked.locked);
  EXPECT_FALSE(locked.canMount);
  fake.objects[QDBusObjectPath(volumePath)][encrypted]["CleartextDevice"] =
      QVariant::fromValue(QDBusObjectPath(clearPath));
  fake.objects[QDBusObjectPath(clearPath)][blockInterface] = {
      {"Drive", QVariant::fromValue(QDBusObjectPath("/"))},
      {"IdUsage", "filesystem"},
      {"CryptoBackingDevice",
       QVariant::fromValue(QDBusObjectPath(volumePath))}};
  fake.objects[QDBusObjectPath(clearPath)][fsInterface] = {
      {"MountPoints", QVariant::fromValue(QList<QByteArray>{})}};
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.volumes()->rowCount() == 2; }));
  EXPECT_FALSE(c.volumes()->find(locked.id)->locked);
  for (const auto &v : c.volumes()->items())
    if (v.id != locked.id) {
      EXPECT_EQ(v.cryptoBackingId, locked.id);
      EXPECT_EQ(v.driveId, locked.driveId);
      EXPECT_TRUE(v.canMount);
    }
}
TEST_F(UDisksTest, MountReplyForRemovedVolumeCannotNavigateUsingStalePath) {
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto id = c.volumes()->items().first().id;
  QSignalSpy spy(&c, &StorageController::operationFinished);
  fake.holdOperations = true;
  c.mount(id);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return fake.pendingOperations.size() == 1; }));
  const auto message = fake.pendingOperations.takeFirst();
  // Enumeration discovers disappearance even if the removal signal was lost.
  fake.objects.remove(QDBusObjectPath(volumePath));
  bus.send(message.createReply(QVariantList{QStringLiteral("/media/stale")}));
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.size() == 1; }));
  const auto result = qvariant_cast<StorageResult>(spy.first().first());
  EXPECT_FALSE(result.succeeded());
  EXPECT_TRUE(result.mountPath.isEmpty());
}

TEST_F(UDisksTest, RotationRatePreservesAbsenceAndUpdatesStableRows) {
  fake.objects[QDBusObjectPath(drivePath)][driveInterface]["RotationRate"] = 0;
  UDisks2Backend backend(QDBusConnection::sessionBus(), service);
  StorageController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  auto *model = c.drives();
  const QPersistentModelIndex index(model->index(0));
  const auto id = model->items().first().id;
  EXPECT_EQ(model->items().first().rotationRate, 0);
  EXPECT_EQ(model->roleNames().value(StorageDriveModel::RotationRateRole),
            "rotationRate");
  EXPECT_EQ(index.data(StorageDriveModel::RotationRateRole).toInt(), 0);
  QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
  auto &props = fake.objects[QDBusObjectPath(drivePath)][driveInterface];
  for (int rate : {-1, 7200, 0}) {
    props["RotationRate"] = rate;
    auto signal = QDBusMessage::createSignal(
        drivePath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << driveInterface << QVariantMap{{"RotationRate", rate}} << QStringList{};
    bus.send(signal);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return model->items().first().rotationRate == rate; }));
    EXPECT_EQ(index.data(StorageDriveModel::RotationRateRole).toInt(), rate);
    EXPECT_EQ(model->items().first().id, id);
    EXPECT_EQ(model->rowCount(), 1);
  }
  props.remove("RotationRate");
  auto signal = QDBusMessage::createSignal(
      drivePath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
  signal << driveInterface << QVariantMap{} << QStringList{"RotationRate"};
  bus.send(signal);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model->items().first().rotationRate; }));
  EXPECT_FALSE(index.data(StorageDriveModel::RotationRateRole).isValid());
  EXPECT_TRUE(index.isValid());
  EXPECT_EQ(changes.count(), 4);
}
