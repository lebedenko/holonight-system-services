#include "BlueZAgent.h"

#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
namespace HoloNight::System {
BlueZAgent::BlueZAgent(QDBusConnection connection, QObject* parent) : QObject(parent), connection_(connection) {}
bool BlueZAgent::authorized() {
  if (calledFromDBus() && !owner_.isEmpty() && message().service() == owner_) return true;
  if (calledFromDBus()) sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Untrusted caller"));
  return false;
}
void BlueZAgent::setOwner(const QString& owner) {
  clear();
  owner_ = owner;
}
void BlueZAgent::setDevices(const QHash<QString, QString>& devices) {
  devices_ = devices;
  const auto pending = pending_;
  for (const auto& p : pending)
    if (!devices_.values().contains(p.request.deviceId)) respond(p.request.requestId, false, {});
}
void BlueZAgent::clear() {
  const auto ids = pending_.keys();
  for (const auto& id : ids) respond(id, false, {});
}
void BlueZAgent::request(const QDBusObjectPath& device, BluetoothInteraction kind, const QString& code,
                         const QString& uuid, quint16 entered, bool delayed) {
  if (!authorized()) return;
  auto deviceId = devices_.value(device.path());
  if (deviceId.isEmpty() && resolver_) {
    deviceId = resolver_(device.path());
    if (!deviceId.isEmpty()) devices_.insert(device.path(), deviceId);
  }
  if (deviceId.isEmpty()) {
    sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Unknown device"));
    return;
  }
  // DisplayPasskey updates belong to the existing display interaction.
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if (!it->delayed && it->request.deviceId == deviceId && it->request.interaction == kind) {
      it->request.code = code;
      it->request.entered = entered;
      emit requested(it->request);
      return;
    }
  }
  BluetoothAgentRequest r{QUuid::createUuid().toString(QUuid::WithoutBraces), deviceId, kind, code, uuid, entered};
  if (delayed) setDelayedReply(true);
  pending_.insert(r.requestId, {r, message(), delayed});
  QTimer::singleShot(timeout_, this, [this, id = r.requestId] { respond(id, false, {}); });
  emit requested(r);
}
void BlueZAgent::respond(const QString& id, bool accept, const QString& input) {
  if (!pending_.contains(id)) return;
  const auto p = pending_.value(id);
  QVariantList arguments;
  if (accept && p.request.interaction == BluetoothInteraction::PinEntry) {
    if (input.isEmpty() || input.size() > 16 || input.contains(QChar::Null)) return;
    arguments << input;
  }
  if (accept && p.request.interaction == BluetoothInteraction::PasskeyEntry) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,6}$"));
    if (!digits.match(input).hasMatch()) return;
    arguments << input.toUInt();
  }
  pending_.remove(id);
  if (p.delayed) {
    connection_.send(accept ? p.message.createReply(arguments)
                            : p.message.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                                                         QStringLiteral("Pairing canceled")));
  }
  emit canceled(id);
}
void BlueZAgent::Release() {
  if (authorized()) clear();
}
void BlueZAgent::Cancel() {
  if (authorized()) clear();
}
QString BlueZAgent::RequestPinCode(const QDBusObjectPath& device) {
  request(device, BluetoothInteraction::PinEntry);
  return {};
}
void BlueZAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pin) {
  request(device, BluetoothInteraction::PinDisplay, pin, {}, 0, false);
}
quint32 BlueZAgent::RequestPasskey(const QDBusObjectPath& device) {
  request(device, BluetoothInteraction::PasskeyEntry);
  return 0;
}
void BlueZAgent::DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered) {
  request(device, BluetoothInteraction::PasskeyDisplay, QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0')), {},
          entered, false);
}
void BlueZAgent::RequestConfirmation(const QDBusObjectPath& device, quint32 passkey) {
  request(device, BluetoothInteraction::Confirmation, QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0')));
}
void BlueZAgent::RequestAuthorization(const QDBusObjectPath& device) {
  request(device, BluetoothInteraction::PairingAuthorization);
}
void BlueZAgent::AuthorizeService(const QDBusObjectPath& device, const QString& uuid) {
  request(device, BluetoothInteraction::ServiceAuthorization, {}, uuid);
}
}  // namespace HoloNight::System
