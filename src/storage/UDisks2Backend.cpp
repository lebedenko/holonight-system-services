#include "UDisks2Backend.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <utility>

namespace HoloNight::System {
namespace {
const QString root = QStringLiteral("/org/freedesktop/UDisks2");
const QString manager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString driveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString blockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString filesystemInterface =
    QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString partitionInterface =
    QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString encryptedInterface =
    QStringLiteral("org.freedesktop.UDisks2.Encrypted");
QString pathValue(const QVariant &value) {
  return qvariant_cast<QDBusObjectPath>(value).path();
}
QString bytePath(QByteArray value) {
  while (value.endsWith('\0'))
    value.chop(1);
  return QFile::decodeName(value);
}
QStringList mountPoints(const QVariant &value) {
  const auto paths = qdbus_cast<QList<QByteArray>>(value);
  QStringList result;
  for (const auto &path : paths)
    result.append(bytePath(path));
  return result;
}
QString key(const QString &path, const QString &interface) {
  return path + QLatin1Char('#') + interface;
}
} // namespace
UDisks2Backend::UDisks2Backend(QObject *parent)
    : UDisks2Backend(QDBusConnection::systemBus(),
                     QStringLiteral("org.freedesktop.UDisks2"), parent) {}
UDisks2Backend::UDisks2Backend(QDBusConnection connection, QString service,
                               QObject *parent)
    : StorageBackend(parent), connection_(std::move(connection)),
      service_(std::move(service)) {
  qDBusRegisterMetaType<StorageInterfaces>();
  qDBusRegisterMetaType<StorageObjects>();
  qDBusRegisterMetaType<QList<QByteArray>>();
  auto *watcher = new QDBusServiceWatcher(
      service_, connection_, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          &UDisks2Backend::ownerChanged);
}
void UDisks2Backend::start() {
  if (started_)
    return;
  started_ = true;
  connection_.connect(
      service_, root, manager, QStringLiteral("InterfacesAdded"), this,
      SLOT(interfacesAdded(QDBusObjectPath,
                           HoloNight::System::StorageInterfaces)));
  connection_.connect(service_, root, manager,
                      QStringLiteral("InterfacesRemoved"), this,
                      SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
  connection_.connect(
      service_, {}, properties, QStringLiteral("PropertiesChanged"), this,
      SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  auto message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("/org/freedesktop/DBus"),
      QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  message << service_;
  const auto generation = generation_;
  auto *call =
      new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation](QDBusPendingCallWatcher *watcher) {
            const QDBusPendingReply<QString> reply = *watcher;
            watcher->deleteLater();
            if (!started_ || generation != generation_)
              return;
            if (reply.isError()) {
              if (reply.error().name() ==
                  QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
                setOwner(service_); // Let the first discovery call activate an
                                    // installed UDisks service.
              else
                emit snapshotChanged({}, {}, false);
            } else
              setOwner(reply.value());
          });
}
void UDisks2Backend::stop() {
  if (!started_)
    return;
  started_ = false;
  connection_.disconnect(
      service_, root, manager, QStringLiteral("InterfacesAdded"), this,
      SLOT(interfacesAdded(QDBusObjectPath,
                           HoloNight::System::StorageInterfaces)));
  connection_.disconnect(service_, root, manager,
                         QStringLiteral("InterfacesRemoved"), this,
                         SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
  connection_.disconnect(
      service_, {}, properties, QStringLiteral("PropertiesChanged"), this,
      SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  setOwner({});
}
void UDisks2Backend::ownerChanged(const QString &, const QString &,
                                  const QString &owner) {
  if (started_)
    setOwner(owner);
}
void UDisks2Backend::setOwner(const QString &owner) {
  ++generation_;
  ++revision_;
  refreshing_ = false;
  owner_ = owner;
  ids_.clear();
  completions_.clear();
  emit snapshotChanged({}, {}, false);
  if (!owner_.isEmpty())
    refresh();
}
void UDisks2Backend::interfacesAdded(const QDBusObjectPath &,
                                     const StorageInterfaces &) {
  changed();
}
void UDisks2Backend::interfacesRemoved(const QDBusObjectPath &path,
                                       const QStringList &interfaces) {
  for (const auto &interface : interfaces)
    ids_.remove(key(path.path(), interface));
  changed();
}
void UDisks2Backend::propertiesChanged(const QString &, const QVariantMap &,
                                       const QStringList &,
                                       const QDBusMessage &) {
  // A fresh authoritative snapshot also resolves invalidated properties.
  changed();
}
void UDisks2Backend::changed() {
  ++revision_;
  if (started_ && !owner_.isEmpty())
    refresh();
}
void UDisks2Backend::refresh() {
  if (refreshing_ || owner_.isEmpty())
    return;
  refreshing_ = true;
  const auto generation = generation_;
  const auto revision = revision_;
  auto message = QDBusMessage::createMethodCall(
      owner_, root, manager, QStringLiteral("GetManagedObjects"));
  auto *call =
      new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, revision](QDBusPendingCallWatcher *watcher) {
            const QDBusPendingReply<StorageObjects> reply = *watcher;
            watcher->deleteLater();
            if (generation != generation_)
              return;
            refreshing_ = false;
            if (revision != revision_) {
              refresh();
              return;
            }
            if (reply.isError()) {
              ids_.clear();
              completions_.clear();
              emit snapshotChanged({}, {}, false);
              return;
            }
            publish(reply.value());
            const auto completions = std::exchange(completions_, {});
            for (auto result : completions) {
              if (result.operation == StorageOperation::Mount &&
                  !ids_.values().contains(result.targetId)) {
                result.errorName =
                    QStringLiteral("org.holonight.Storage.Disappeared");
                result.errorMessage =
                    QStringLiteral("Volume disappeared before mount completed");
                result.mountPath.clear();
              }
              emit operationFinished(result);
            }
          });
}
QString UDisks2Backend::idFor(const QString &path, const QString &interface) {
  auto &id = ids_[key(path, interface)];
  if (id.isEmpty())
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return id;
}
void UDisks2Backend::publish(const StorageObjects &objects) {
  QSet<QString> live;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    for (const auto &interface : {driveInterface, blockInterface})
      if (it->contains(interface)) {
        live.insert(key(it.key().path(), interface));
        idFor(it.key().path(), interface);
      }
  for (auto it = ids_.begin(); it != ids_.end();) {
    if (!live.contains(it.key()))
      it = ids_.erase(it);
    else
      ++it;
  }
  QList<StorageDrive> drives;
  QList<StorageVolume> volumes;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
    const auto &interfaces = it.value();
    const auto path = it.key().path();
    if (interfaces.contains(driveInterface)) {
      const auto props = interfaces.value(driveInterface);
      StorageDrive drive;
      drive.id = ids_.value(key(path, driveInterface));
      drive.vendor = props.value(QStringLiteral("Vendor")).toString();
      drive.model = props.value(QStringLiteral("Model")).toString();
      drive.serial = props.value(QStringLiteral("Serial")).toString();
      drive.connectionBus =
          props.value(QStringLiteral("ConnectionBus")).toString();
      drive.siblingId = props.value(QStringLiteral("SiblingId")).toString();
      drive.removable = props.value(QStringLiteral("Removable")).toBool();
      drive.mediaRemovable =
          props.value(QStringLiteral("MediaRemovable")).toBool();
      drive.mediaPresent =
          props.value(QStringLiteral("MediaAvailable")).toBool();
      drive.optical = props.value(QStringLiteral("Optical")).toBool();
      drive.canEject = props.value(QStringLiteral("Ejectable")).toBool();
      drive.canPowerOff = props.value(QStringLiteral("CanPowerOff")).toBool();
      drives.append(drive);
    }
    if (interfaces.contains(blockInterface)) {
      const auto props = interfaces.value(blockInterface);
      StorageVolume volume;
      volume.id = ids_.value(key(path, blockInterface));
      volume.driveId = ids_.value(
          key(pathValue(props.value(QStringLiteral("Drive"))), driveInterface));
      volume.cryptoBackingId = ids_.value(
          key(pathValue(props.value(QStringLiteral("CryptoBackingDevice"))),
              blockInterface));
      volume.label = props.value(QStringLiteral("IdLabel")).toString();
      volume.device = bytePath(
          props.value(QStringLiteral("PreferredDevice")).toByteArray());
      volume.filesystemType = props.value(QStringLiteral("IdType")).toString();
      volume.usage = props.value(QStringLiteral("IdUsage")).toString();
      volume.capacity = props.value(QStringLiteral("Size")).toULongLong();
      volume.hintIgnore = props.value(QStringLiteral("HintIgnore")).toBool();
      volume.hintSystem = props.value(QStringLiteral("HintSystem")).toBool();
      volume.loop =
          interfaces.contains(QStringLiteral("org.freedesktop.UDisks2.Loop"));
      const auto tablePath = pathValue(
          interfaces.value(partitionInterface).value(QStringLiteral("Table")));
      if (!tablePath.isEmpty())
        volume.loop =
            volume.loop ||
            objects.value(QDBusObjectPath(tablePath))
                .contains(QStringLiteral("org.freedesktop.UDisks2.Loop"));
      const auto partition = interfaces.value(partitionInterface);
      volume.partitionType = partition.value(QStringLiteral("Type")).toString();
      volume.partitionContainer =
          partition.value(QStringLiteral("IsContainer")).toBool() ||
          (interfaces.contains(
               QStringLiteral("org.freedesktop.UDisks2.PartitionTable")) &&
           !interfaces.contains(filesystemInterface));
      volume.locked =
          interfaces.contains(encryptedInterface) &&
          pathValue(interfaces.value(encryptedInterface)
                        .value(QStringLiteral("CleartextDevice"))) ==
              QLatin1String("/");
      if (interfaces.contains(filesystemInterface)) {
        volume.mountPoints =
            mountPoints(interfaces.value(filesystemInterface)
                            .value(QStringLiteral("MountPoints")));
        volume.canMount = volume.mountPoints.isEmpty();
        volume.canUnmount = !volume.mountPoints.isEmpty();
      }
      volumes.append(volume);
    }
  }
  // Cleartext mappings can omit Drive; inherit the backing device's association
  // and loop hint.
  for (int pass = 0; pass < volumes.size(); ++pass) {
    bool changed = false;
    for (auto &volume : volumes)
      for (const auto &backing : volumes)
        if (!volume.cryptoBackingId.isEmpty() &&
            backing.id == volume.cryptoBackingId) {
          if (volume.driveId.isEmpty() && !backing.driveId.isEmpty()) {
            volume.driveId = backing.driveId;
            changed = true;
          }
          if (!volume.loop && backing.loop) {
            volume.loop = true;
            changed = true;
          }
        }
    if (!changed)
      break;
  }
  emit snapshotChanged(drives, volumes, true);
}
void UDisks2Backend::execute(const QString &requestId,
                             StorageOperation operation,
                             const QString &targetId) {
  const bool drive = operation == StorageOperation::Eject ||
                     operation == StorageOperation::PowerOff;
  const auto interface = drive ? driveInterface : blockInterface;
  QString path;
  for (auto it = ids_.cbegin(); it != ids_.cend(); ++it)
    if (it.value() == targetId &&
        it.key().endsWith(QLatin1Char('#') + interface))
      path = it.key().section(QLatin1Char('#'), 0, 0);
  StorageResult result{requestId, targetId, operation, {}, {}, {}};
  if (path.isEmpty() || owner_.isEmpty()) {
    result.errorName = QStringLiteral("org.holonight.Storage.Disappeared");
    result.errorMessage = QStringLiteral("Storage target disappeared");
    QTimer::singleShot(0, this,
                       [this, result] { emit operationFinished(result); });
    return;
  }
  QString method;
  switch (operation) {
  case StorageOperation::Mount:
    method = QStringLiteral("Mount");
    break;
  case StorageOperation::Unmount:
    method = QStringLiteral("Unmount");
    break;
  case StorageOperation::Eject:
    method = QStringLiteral("Eject");
    break;
  case StorageOperation::PowerOff:
    method = QStringLiteral("PowerOff");
    break;
  }
  auto message = QDBusMessage::createMethodCall(
      owner_, path, drive ? driveInterface : filesystemInterface, method);
  message.setInteractiveAuthorizationAllowed(true);
  message << QVariantMap{}; // Never force, lazily unmount, or suppress polkit.
  const auto generation = generation_;
  auto *call =
      new QDBusPendingCallWatcher(connection_.asyncCall(message, 120000), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, result](QDBusPendingCallWatcher *watcher) mutable {
            const auto reply = watcher->reply();
            watcher->deleteLater();
            if (generation != generation_)
              return;
            if (reply.type() == QDBusMessage::ErrorMessage) {
              result.errorName = reply.errorName();
              result.errorMessage = reply.errorMessage();
            } else if (result.operation == StorageOperation::Mount &&
                       !reply.arguments().isEmpty()) {
              result.mountPath = reply.arguments().first().toString();
            }
            if (result.operation == StorageOperation::Mount &&
                !ids_.values().contains(result.targetId)) {
              result.errorName =
                  QStringLiteral("org.holonight.Storage.Disappeared");
              result.errorMessage =
                  QStringLiteral("Volume disappeared before mount completed");
              result.mountPath.clear();
            }
            completions_.append(result);
            changed(); // Publish refreshed topology before controller advances
                       // its next step.
          });
}
} // namespace HoloNight::System
