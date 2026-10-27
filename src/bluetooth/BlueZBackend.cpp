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
BlueZBackend::BlueZBackend(QDBusConnection connection, QString service, QObject* parent)
    : BluetoothBackend(parent),
      connection_(connection),
      service_(std::move(service)),
      agent_path_(QStringLiteral("/org/holonight/bluetooth/agent_") + uuid()),
      agent_(connection, this) {
  // A trusted Agent1 call may arrive before the ObjectManager refresh for a new
  // incoming device. Reserve its lifetime ID immediately and reconcile the graph.
  agent_.setDeviceResolver([this](const QString& path) {
    const auto id = idFor(path);
    changed();
    return id;
  });
  qDBusRegisterMetaType<BluetoothInterfaces>();
  qDBusRegisterMetaType<BluetoothObjects>();
  auto* watcher = new QDBusServiceWatcher(service_, connection_, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          [this](const QString&, const QString&, const QString& owner) {
            if (started_) setOwner(owner);
          });
  connect(&agent_, &BlueZAgent::requested, this, &BluetoothBackend::agentRequested);
  connect(&agent_, &BlueZAgent::canceled, this, &BluetoothBackend::agentCanceled);
}
void BlueZBackend::start() {
  if (started_) return;
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
    if (!started_ || generation != generation_) return;
    setOwner(reply.isError() ? QString{} : reply.value());
  });
}
void BlueZBackend::stop() {
  if (!started_) return;
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
    if (generation != generation_) return;
    if (reply.isError())
      emit diagnostic(reply.error().name(), reply.error().message());
    else {
      registered_ = true;
      if (default_agent_) requestDefaultAgent();
    }
  });
}
void BlueZBackend::requestDefaultAgent() {
  default_agent_ = true;
  if (!registered_) return;
  auto message = QDBusMessage::createMethodCall(owner_, QStringLiteral("/org/bluez"), agentManager,
                                                QStringLiteral("RequestDefaultAgent"));
  message << QVariant::fromValue(QDBusObjectPath(agent_path_));
  const auto generation = generation_;
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* watcher) {
    QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();
    if (generation == generation_ && reply.isError()) emit diagnostic(reply.error().name(), reply.error().message());
  });
}
void BlueZBackend::respond(const QString& id, bool accept, const QString& input) { agent_.respond(id, accept, input); }
void BlueZBackend::interfacesAdded(const QDBusObjectPath&, const BluetoothInterfaces&, const QDBusMessage& message) {
  if (message.service() == owner_) changed();
}
void BlueZBackend::interfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces,
                                     const QDBusMessage& message) {
  if (message.service() != owner_) return;
  if (interfaces.contains(adapterInterface)) {
    for (auto it = device_adapters_.cbegin(); it != device_adapters_.cend(); ++it)
      if (it.value() == path.path()) ids_.remove(it.key());
    discovery_owned_.remove(path.path());
    discovery_pending_.remove(path.path());
  }
  if (interfaces.contains(adapterInterface) || interfaces.contains(deviceInterface)) ids_.remove(path.path());
  changed();
}
void BlueZBackend::propertiesChanged(const QString&, const QVariantMap&, const QStringList&,
                                     const QDBusMessage& message) {
  if (message.service() == owner_) changed();
}
void BlueZBackend::changed() {
  ++revision_;
  if (started_ && !owner_.isEmpty()) refresh();
}
void BlueZBackend::refresh() {
  if (refreshing_ || owner_.isEmpty()) return;
  refreshing_ = true;
  const auto generation = generation_, revision = revision_;
  auto message =
      QDBusMessage::createMethodCall(owner_, QStringLiteral("/"), manager, QStringLiteral("GetManagedObjects"));
  auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, revision](QDBusPendingCallWatcher* watcher) {
            QDBusPendingReply<BluetoothObjects> reply = *watcher;
            watcher->deleteLater();
            if (generation != generation_) return;
            refreshing_ = false;
            if (revision != revision_) {
              refresh();
              return;
            }
            if (reply.isError()) {
              for (const auto& path : discovery_owned_)
                connection_.asyncCall(
                    QDBusMessage::createMethodCall(owner_, path, adapterInterface, QStringLiteral("StopDiscovery")));
              discovery_owned_.clear();
              ids_.clear();
              agent_.setDevices({});
              emit snapshotChanged({}, {}, false);
            } else
              publish(reply.value());
          });
}
QString BlueZBackend::idFor(const QString& path) {
  if (!ids_.contains(path)) ids_.insert(path, uuid());
  return ids_.value(path);
}
void BlueZBackend::publish(const BluetoothObjects& objects) {
  QSet<QString> live;
  adapters_.clear();
  device_adapters_.clear();
  QList<BluetoothAdapter> adapters;
  QList<BluetoothDevice> devices;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    if (it->contains(adapterInterface)) {
      const auto& p = it->value(adapterInterface);
      const auto path = it.key().path();
      live.insert(path);
      adapters_.insert(path);
      adapters.append({idFor(path), p.value("Address").toString(), p.value("Alias").toString(),
                       p.value("Powered").toBool(), p.value("Discoverable").toBool(), p.value("Discovering").toBool()});
      if (!p.value("Powered").toBool()) discovery_owned_.remove(path);
    }
  QHash<QString, QString> agentDevices;
  for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    if (it->contains(deviceInterface)) {
      const auto& p = it->value(deviceInterface);
      const auto path = it.key().path();
      const auto adapter = qvariant_cast<QDBusObjectPath>(p.value("Adapter")).path();
      if (!adapters_.contains(adapter)) continue;
      live.insert(path);
      device_adapters_.insert(path, adapter);
      BluetoothDevice d;
      d.id = idFor(path);
      d.adapterId = idFor(adapter);
      d.address = p.value("Address").toString();
      d.addressType = p.value("AddressType").toString();
      d.name = p.value("Name").toString();
      d.alias = p.value("Alias").toString();
      d.icon = p.value("Icon").toString();
      d.deviceClass = p.value("Class").toUInt();
      d.appearance = p.value("Appearance").value<quint16>();
      d.paired = p.value("Paired").toBool();
      d.bonded = p.value("Bonded").toBool();
      d.trusted = p.value("Trusted").toBool();
      d.blocked = p.value("Blocked").toBool();
      d.connected = p.value("Connected").toBool();
      if (p.contains("RSSI")) d.rssi = p.value("RSSI").toInt();
      const auto battery = it->value(QStringLiteral("org.bluez.Battery1"));
      if (battery.contains("Percentage")) d.battery = battery.value("Percentage").toInt();
      d.uuids = qdbus_cast<QStringList>(p.value("UUIDs"));
      devices.append(d);
      agentDevices.insert(path, d.id);
    }
  for (auto it = ids_.begin(); it != ids_.end();) {
    if (!live.contains(it.key()))
      it = ids_.erase(it);
    else
      ++it;
  }
  agent_.setDevices(agentDevices);
  emit snapshotChanged(adapters, devices, true);
}
void BlueZBackend::execute(const QString& requestId, BluetoothOperation operation, const QString& targetId,
                           const QVariant& value) {
  BluetoothResult result{requestId, targetId, operation, {}, {}};
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
  QString member, callPath = path, interface = adapter ? adapterInterface : deviceInterface;
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
    auto timeout = QDBusMessage::createMethodCall(owner_, path, properties, QStringLiteral("Set"));
    timeout << adapterInterface << QStringLiteral("DiscoverableTimeout")
            << QVariant::fromValue(QDBusVariant(quint32(180)));
    auto* call = new QDBusPendingCallWatcher(connection_.asyncCall(timeout), this);
    connect(call, &QDBusPendingCallWatcher::finished, this,
            [this, generation, targetId, requestId, operation, value](QDBusPendingCallWatcher* watcher) {
              QDBusPendingReply<> reply = *watcher;
              watcher->deleteLater();
              if (generation != generation_) return;
              if (reply.isError())
                emit operationFinished({requestId, targetId, operation, reply.error().name(), reply.error().message()});
              else {
                // Enable visibility only after the timeout write succeeds.
                const auto path = ids_.key(targetId);
                if (path.isEmpty()) {
                  emit operationFinished(
                      {requestId, targetId, operation, QStringLiteral("org.holonight.Bluetooth.Disappeared"), {}});
                  return;
                }
                auto set = QDBusMessage::createMethodCall(owner_, path, properties, QStringLiteral("Set"));
                set << adapterInterface << QStringLiteral("Discoverable") << QVariant::fromValue(QDBusVariant(value));
                auto* next = new QDBusPendingCallWatcher(connection_.asyncCall(set), this);
                connect(next, &QDBusPendingCallWatcher::finished, this,
                        [this, generation, requestId, targetId, operation](QDBusPendingCallWatcher* w) {
                          QDBusPendingReply<> r = *w;
                          w->deleteLater();
                          if (generation != generation_) return;
                          emit operationFinished({requestId, targetId, operation,
                                                  r.isError() ? r.error().name() : QString{},
                                                  r.isError() ? r.error().message() : QString{}});
                          changed();
                        });
              }
            });
    return;
  }
  auto message = QDBusMessage::createMethodCall(owner_, callPath, interface, member);
  message.setArguments(arguments);
  if (discovery) discovery_pending_.insert(path);
  auto* call = new QDBusPendingCallWatcher(
      connection_.asyncCall(message, operation == BluetoothOperation::Pair ? 120000 : 25000), this);
  connect(call, &QDBusPendingCallWatcher::finished, this,
          [this, generation, path, result, discovery](QDBusPendingCallWatcher* watcher) mutable {
            QDBusPendingReply<> reply = *watcher;
            watcher->deleteLater();
            if (generation != generation_) return;
            if (discovery) discovery_pending_.remove(path);
            if (ids_.value(path) != result.targetId && result.operation != BluetoothOperation::Forget) {
              if (!reply.isError() && result.operation == BluetoothOperation::StartDiscovery)
                connection_.asyncCall(
                    QDBusMessage::createMethodCall(owner_, path, adapterInterface, QStringLiteral("StopDiscovery")));
              return;
            }
            if (reply.isError()) {
              result.errorName = reply.error().name();
              result.errorMessage = reply.error().message();
            } else if (discovery) {
              if (result.operation == BluetoothOperation::StartDiscovery)
                discovery_owned_.insert(path);
              else
                discovery_owned_.remove(path);
            }
            if (result.operation == BluetoothOperation::Pair) agent_.clear();
            emit operationFinished(result);
            changed();
          });
}
}  // namespace HoloNight::System
