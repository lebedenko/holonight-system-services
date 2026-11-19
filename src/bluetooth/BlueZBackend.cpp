#include "BlueZBackend.h"

#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTimer>
#include <QUuid>

#include <algorithm>
namespace HoloNight::System {
namespace {
const QString manager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString deviceInterface = QStringLiteral("org.bluez.Device1");
const QString agentManager = QStringLiteral("org.bluez.AgentManager1");
QString uuid() { return QUuid::createUuid().toString(QUuid::Id128); }
}  // namespace
BlueZBackend::BlueZBackend(QObject* parent)
    : BlueZBackend(QDBusConnection::systemBus(), QStringLiteral("org.bluez"), parent) {}
BlueZBackend::BlueZBackend(const QDBusConnection& connection, QString service, QObject* parent)
    : BluetoothBackend(parent),
      connection_(connection),
      service_(std::move(service)),
      agent_path_(QStringLiteral("/org/holonight/bluetooth/agent_") + uuid()),
      agent_(connection, this) {
  // A trusted Agent1 call may arrive before the ObjectManager refresh for a new
  // incoming device. Reserve its lifetime ID immediately and reconcile the graph.
  agent_.setDeviceResolver([this](const QString& path) {
    const auto identifier = idFor(path);
    changed();
    return identifier;
  });
  qDBusRegisterMetaType<BluetoothInterfaces>();
  qDBusRegisterMetaType<BluetoothObjects>();
  auto* watcher = new QDBusServiceWatcher(service_, connection_, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          [this](const QString&, const QString&, const QString& owner) {
            if (started_) {
              setOwner(owner);
            }
          });
  connect(&agent_, &BlueZAgent::requested, this, &BluetoothBackend::agentRequested);
  connect(&agent_, &BlueZAgent::canceled, this, &BluetoothBackend::agentCanceled);
}
void BlueZBackend::start() {
  if (started_) {
    return;
  }
  started_ = true;
  connection_.registerObject(agent_path_, &agent_, QDBusConnection::ExportAllSlots);
  connection_.connect(service_, QStringLiteral("/"), manager, QStringLiteral("InterfacesAdded"), this,
                      SLOT(interfacesAdded(QDBusObjectPath, HoloNight::System::BluetoothInterfaces, QDBusMessage)));
  connection_.connect(service_, QStringLiteral("/"), manager, QStringLiteral("InterfacesRemoved"), this,
                      SLOT(interfacesRemoved(QDBusObjectPath, QStringList, QDBusMessage)));
  connection_.connect(service_, {}, properties, QStringLiteral("PropertiesChanged"), this,
                      SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  auto message =
      QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                     QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  message << service_;
  const auto generation = generation_;
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* watcher) {
    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();
    if (!started_ || generation != generation_) {
      return;
    }
    setOwner(reply.isError() ? QString{} : reply.value());
  });
}
void BlueZBackend::stop() {
  if (!started_) {
    return;
  }
  agent_.clear();
  for (const auto& path : (discovery_owned_ | discovery_pending_)) {
    auto message = QDBusMessage::createMethodCall(owner_, path, adapterInterface, QStringLiteral("StopDiscovery"));
    connection_.asyncCall(message);
  }
  if (registered_) {
    auto message = QDBusMessage::createMethodCall(owner_, QStringLiteral("/org/bluez"), agentManager,
                                                  QStringLiteral("UnregisterAgent"));
    message << QVariant::fromValue(QDBusObjectPath(agent_path_));
    connection_.asyncCall(message);
  }
  connection_.disconnect(service_, QStringLiteral("/"), manager, QStringLiteral("InterfacesAdded"), this,
                         SLOT(interfacesAdded(QDBusObjectPath, HoloNight::System::BluetoothInterfaces, QDBusMessage)));
  connection_.disconnect(service_, QStringLiteral("/"), manager, QStringLiteral("InterfacesRemoved"), this,
                         SLOT(interfacesRemoved(QDBusObjectPath, QStringList, QDBusMessage)));
  connection_.disconnect(service_, {}, properties, QStringLiteral("PropertiesChanged"), this,
                         SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
  connection_.unregisterObject(agent_path_);
  started_ = false;
  setOwner({});
}
void BlueZBackend::setOwner(const QString& owner) {
  ++generation_;
  ++revision_;
  refreshing_ = false;
  registered_ = false;
  owner_ = owner;
  ids_.clear();
  adapters_.clear();
  device_adapters_.clear();
  discovery_owned_.clear();
  discovery_pending_.clear();
  agent_.setOwner(owner);
  emit snapshotChanged({}, {}, false);
  if (!owner.isEmpty()) {
    registerAgent();
    refresh();
  }
}
void BlueZBackend::registerAgent() {
  auto message = QDBusMessage::createMethodCall(owner_, QStringLiteral("/org/bluez"), agentManager,
                                                QStringLiteral("RegisterAgent"));
  message << QVariant::fromValue(QDBusObjectPath(agent_path_)) << QStringLiteral("KeyboardDisplay");
  const auto generation = generation_;
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* watcher) {
    QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();
    if (generation != generation_) {
      return;
    }
    if (reply.isError()) {
      emit diagnostic(reply.error().name(), reply.error().message());
    } else {
      registered_ = true;
      if (default_agent_) {
        requestDefaultAgent();
      }
    }
  });
}
void BlueZBackend::requestDefaultAgent() {
  default_agent_ = true;
  if (!registered_) {
    return;
  }
  auto message = QDBusMessage::createMethodCall(owner_, QStringLiteral("/org/bluez"), agentManager,
                                                QStringLiteral("RequestDefaultAgent"));
  message << QVariant::fromValue(QDBusObjectPath(agent_path_));
  const auto generation = generation_;
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* watcher) {
    QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();
    if (generation == generation_ && reply.isError()) {
      emit diagnostic(reply.error().name(), reply.error().message());
    }
  });
}
void BlueZBackend::respond(const QString& requestId, bool accept, const QString& input) {
  agent_.respond(requestId, accept, input);
}
void BlueZBackend::interfacesAdded(const QDBusObjectPath& /*unused*/, const BluetoothInterfaces& /*unused*/,
                                   const QDBusMessage& message) {
  if (message.service() == owner_) {
    changed();
  }
}
void BlueZBackend::interfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces,
                                     const QDBusMessage& message) {
  if (message.service() != owner_) {
    return;
  }
  if (interfaces.contains(adapterInterface)) {
    for (auto it = device_adapters_.cbegin(); it != device_adapters_.cend(); ++it) {
      if (it.value() == path.path()) {
        ids_.remove(it.key());
      }
    }
    discovery_owned_.remove(path.path());
    discovery_pending_.remove(path.path());
  }
  if (interfaces.contains(adapterInterface) || interfaces.contains(deviceInterface)) {
    ids_.remove(path.path());
  }
  changed();
}
void BlueZBackend::propertiesChanged(const QString& /*unused*/, const QVariantMap& /*unused*/,
                                     const QStringList& /*unused*/, const QDBusMessage& message) {
  if (message.service() == owner_) {
    changed();
  }
}
void BlueZBackend::changed() {
  ++revision_;
  if (started_ && !owner_.isEmpty()) {
    refresh();
  }
}
void BlueZBackend::refresh() {
  if (refreshing_ || owner_.isEmpty()) {
    return;
  }
  refreshing_ = true;
  const auto generation = generation_;
  const auto revision = revision_;
  auto message =
      QDBusMessage::createMethodCall(owner_, QStringLiteral("/"), manager, QStringLiteral("GetManagedObjects"));
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, revision](QDBusPendingCallWatcher* watcher) {
            QDBusPendingReply<BluetoothObjects> reply = *watcher;
            watcher->deleteLater();
            if (generation != generation_) {
              return;
            }
            refreshing_ = false;
            if (revision != revision_) {
              refresh();
              return;
            }
            if (reply.isError()) {
              for (const auto& path : discovery_owned_) {
                connection_.asyncCall(
                    QDBusMessage::createMethodCall(owner_, path, adapterInterface, QStringLiteral("StopDiscovery")));
              }
              discovery_owned_.clear();
              ids_.clear();
              agent_.setDevices({});
              emit snapshotChanged({}, {}, false);
            } else {
              publish(reply.value());
            }
          });
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): watcher is parent-owned and uses deleteLater.
QString BlueZBackend::idFor(const QString& path) {
  if (!ids_.contains(path)) {
    ids_.insert(path, uuid());
  }
  return ids_.value(path);
}
void BlueZBackend::publish(const BluetoothObjects& objects) {
  QSet<QString> live;
  adapters_.clear();
  device_adapters_.clear();
  QList<BluetoothAdapter> adapters;
  QList<BluetoothDevice> devices;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
    if (it->contains(adapterInterface)) {
      const auto& propertiesMap = it->value(adapterInterface);
      const auto path = it.key().path();
      live.insert(path);
      adapters_.insert(path);
      adapters.append({
          .id = idFor(path),
          .address = propertiesMap.value("Address").toString(),
          .alias = propertiesMap.value("Alias").toString(),
          .powered = propertiesMap.value("Powered").toBool(),
          .discoverable = propertiesMap.value("Discoverable").toBool(),
          .discovering = propertiesMap.value("Discovering").toBool(),
      });
      if (!propertiesMap.value("Powered").toBool()) {
        discovery_owned_.remove(path);
      }
    }
  }
  QHash<QString, QString> agentDevices;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
    if (it->contains(deviceInterface)) {
      const auto& propertiesMap = it->value(deviceInterface);
      const auto path = it.key().path();
      const auto adapter = qvariant_cast<QDBusObjectPath>(propertiesMap.value("Adapter")).path();
      if (!adapters_.contains(adapter)) {
        continue;
      }
      live.insert(path);
      device_adapters_.insert(path, adapter);
      BluetoothDevice device;
      device.id = idFor(path);
      device.adapterId = idFor(adapter);
      device.address = propertiesMap.value("Address").toString();
      device.addressType = propertiesMap.value("AddressType").toString();
      device.name = propertiesMap.value("Name").toString();
      device.alias = propertiesMap.value("Alias").toString();
      device.icon = propertiesMap.value("Icon").toString();
      device.deviceClass = propertiesMap.value("Class").toUInt();
      device.appearance = propertiesMap.value("Appearance").value<quint16>();
      device.paired = propertiesMap.value("Paired").toBool();
      device.bonded = propertiesMap.value("Bonded").toBool();
      device.trusted = propertiesMap.value("Trusted").toBool();
      device.blocked = propertiesMap.value("Blocked").toBool();
      device.connected = propertiesMap.value("Connected").toBool();
      if (propertiesMap.contains("RSSI")) {
        device.rssi = propertiesMap.value("RSSI").toInt();
      }
      const auto battery = it->value(QStringLiteral("org.bluez.Battery1"));
      if (battery.contains("Percentage")) {
        device.battery = battery.value("Percentage").toInt();
      }
      device.uuids = qdbus_cast<QStringList>(propertiesMap.value("UUIDs"));
      devices.append(device);
      agentDevices.insert(path, device.id);
    }
  }
  for (auto it = ids_.begin(); it != ids_.end();) {
    if (!live.contains(it.key())) {
      it = ids_.erase(it);
    } else {
      ++it;
    }
  }
  agent_.setDevices(agentDevices);
  emit snapshotChanged(adapters, devices, true);
}
void BlueZBackend::execute(const QString& requestId, BluetoothOperation operation, const QString& targetId,
                           const QVariant& value) {
  BluetoothResult result{
      .requestId = requestId,
      .targetId = targetId,
      .operation = operation,
      .errorName = {},
      .errorMessage = {},
  };
  const auto path = ids_.key(targetId);
  auto fail = [this, result](const QString& error) mutable {
    result.errorName = QStringLiteral("org.holonight.Bluetooth.") + error;
    result.errorMessage = error;
    QTimer::singleShot(0, this, [this, result] { emit operationFinished(result); });
  };
  if (path.isEmpty() || owner_.isEmpty()) {
    fail(QStringLiteral("Disappeared"));
    return;
  }
  if (operation == BluetoothOperation::Pair && !registered_) {
    fail(QStringLiteral("AgentUnavailable"));
    return;
  }
  const bool adapter = operation <= BluetoothOperation::StopDiscovery;
  if (adapter != adapters_.contains(path)) {
    fail(QStringLiteral("InvalidTarget"));
    return;
  }
  const bool discovery =
      operation == BluetoothOperation::StartDiscovery || operation == BluetoothOperation::StopDiscovery;
  if (discovery && discovery_pending_.contains(path)) {
    fail(QStringLiteral("Busy"));
    return;
  }
  if (discovery && ((operation == BluetoothOperation::StartDiscovery) == discovery_owned_.contains(path))) {
    QTimer::singleShot(0, this, [this, result] { emit operationFinished(result); });
    return;
  }
  QString member;
  QString callPath = path;
  QString interface = adapter ? adapterInterface : deviceInterface;
  QVariantList arguments;
  QString property;
  switch (operation) {
    case BluetoothOperation::Power:
      property = QStringLiteral("Powered");
      break;
    case BluetoothOperation::AdapterAlias:
    case BluetoothOperation::DeviceAlias:
      property = QStringLiteral("Alias");
      break;
    case BluetoothOperation::Discoverable:
      property = QStringLiteral("Discoverable");
      break;
    case BluetoothOperation::Trust:
      property = QStringLiteral("Trusted");
      break;
    case BluetoothOperation::Block:
      property = QStringLiteral("Blocked");
      break;
    case BluetoothOperation::StartDiscovery:
      member = (QStringLiteral("StartDiscovery"));
      break;
    case BluetoothOperation::StopDiscovery:
      member = (QStringLiteral("StopDiscovery"));
      break;
    case BluetoothOperation::Pair:
      member = (QStringLiteral("Pair"));
      break;
    case BluetoothOperation::CancelPairing:
      member = (QStringLiteral("CancelPairing"));
      agent_.clear();
      break;
    case BluetoothOperation::Connect:
      member = (QStringLiteral("Connect"));
      break;
    case BluetoothOperation::Disconnect:
      member = (QStringLiteral("Disconnect"));
      break;
    case BluetoothOperation::Forget:
      callPath = device_adapters_.value(path);
      interface = adapterInterface;
      member = (QStringLiteral("RemoveDevice"));
      arguments << QVariant::fromValue(QDBusObjectPath(path));
      break;
  }
  if (!property.isEmpty()) {
    interface = properties;
    member = (QStringLiteral("Set"));
    arguments << (adapter ? adapterInterface : deviceInterface) << property << QVariant::fromValue(QDBusVariant(value));
  }
  const auto generation = generation_;
  // Set the visibility timeout before enabling discoverability.
  if (operation == BluetoothOperation::Discoverable && value.toBool()) {
    setDiscoverableTimeout(generation, requestId, targetId, path, value);
    return;
  }
  auto message = QDBusMessage::createMethodCall(owner_, callPath, interface, member);
  message.setArguments(arguments);
  if (discovery) {
    discovery_pending_.insert(path);
  }
  auto* call = new QDBusPendingCallWatcher(
      connection_.asyncCall(message, operation == BluetoothOperation::Pair ? 120000 : 25000), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, path, result, discovery](QDBusPendingCallWatcher* watcher) mutable {
            completeOperation(watcher, generation, path, std::move(result), discovery);
          });
}
void BlueZBackend::completeOperation(QDBusPendingCallWatcher* watcher, quint64 generation, const QString& path,
                                     BluetoothResult result, bool discovery) {
  QDBusPendingReply<> reply = *watcher;
  watcher->deleteLater();
  if (generation != generation_) {
    return;
  }
  if (discovery) {
    discovery_pending_.remove(path);
  }
  if (ids_.value(path) != result.targetId && result.operation != BluetoothOperation::Forget) {
    if (!reply.isError() && result.operation == BluetoothOperation::StartDiscovery) {
      connection_.asyncCall(
          QDBusMessage::createMethodCall(owner_, path, adapterInterface, QStringLiteral("StopDiscovery")));
    }
    return;
  }
  if (reply.isError()) {
    result.errorName = reply.error().name();
    result.errorMessage = reply.error().message();
  } else if (discovery) {
    if (result.operation == BluetoothOperation::StartDiscovery) {
      discovery_owned_.insert(path);
    } else {
      discovery_owned_.remove(path);
    }
  }
  if (result.operation == BluetoothOperation::Pair) {
    agent_.clear();
  }
  emit operationFinished(result);
  changed();
}
void BlueZBackend::setDiscoverable(quint64 generation, const QString& requestId, const QString& targetId,
                                   const QVariant& value) {
  constexpr auto operation = BluetoothOperation::Discoverable;
  // Enable visibility only after the timeout write succeeds.
  const auto path = ids_.key(targetId);
  if (path.isEmpty()) {
    emit operationFinished({
        .requestId = requestId,
        .targetId = targetId,
        .operation = operation,
        .errorName = QStringLiteral("org.holonight.Bluetooth.Disappeared"),
        .errorMessage = {},
    });
    return;
  }
  auto set = QDBusMessage::createMethodCall(owner_, path, properties, QStringLiteral("Set"));
  set << adapterInterface << QStringLiteral("Discoverable") << QVariant::fromValue(QDBusVariant(value));
  auto* next = new QDBusPendingCallWatcher(connection_.asyncCall(set), this);
  connect(next, &QDBusPendingCallWatcher::finished, this,
          [this, generation, requestId, targetId, operation](QDBusPendingCallWatcher* nextWatcher) {
            QDBusPendingReply<> pendingReply = *nextWatcher;
            nextWatcher->deleteLater();
            if (generation != generation_) {
              return;
            }
            emit operationFinished({
                .requestId = requestId,
                .targetId = targetId,
                .operation = operation,
                .errorName = pendingReply.isError() ? pendingReply.error().name() : QString{},
                .errorMessage = pendingReply.isError() ? pendingReply.error().message() : QString{},
            });
            changed();
          });
}
void BlueZBackend::setDiscoverableTimeout(quint64 generation, const QString& requestId, const QString& targetId,
                                          const QString& path, const QVariant& value) {
  constexpr auto operation = BluetoothOperation::Discoverable;
  auto timeout = QDBusMessage::createMethodCall(owner_, path, properties, QStringLiteral("Set"));
  timeout << adapterInterface << QStringLiteral("DiscoverableTimeout")
          << QVariant::fromValue(QDBusVariant(static_cast<quint32>(180)));
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(timeout), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, targetId, requestId, operation, value](QDBusPendingCallWatcher* watcher) {
            QDBusPendingReply<> reply = *watcher;
            watcher->deleteLater();
            if (generation != generation_) {
              return;
            }
            if (reply.isError()) {
              emit operationFinished({
                  .requestId = requestId,
                  .targetId = targetId,
                  .operation = operation,
                  .errorName = reply.error().name(),
                  .errorMessage = reply.error().message(),
              });
            } else {
              setDiscoverable(generation, requestId, targetId, value);
            }
          });
}
}  // namespace HoloNight::System
