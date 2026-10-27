#pragma once
#include "BluetoothTypes.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>

#include <functional>
namespace HoloNight::System {
class BlueZAgent final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
 public:
  explicit BlueZAgent(QDBusConnection connection, QObject* parent = nullptr);
  void setRequestTimeout(int milliseconds) { timeout_ = milliseconds; }
  void setDeviceResolver(std::function<QString(const QString&)> resolver) { resolver_ = std::move(resolver); }
  void setOwner(const QString& owner);
  void setDevices(const QHash<QString, QString>& devices);
  void respond(const QString& id, bool accept, const QString& input);
  void clear();
 signals:
  void requested(BluetoothAgentRequest request);
  void canceled(QString requestId);
 public slots:
  void Release();
  QString RequestPinCode(const QDBusObjectPath& device);
  void DisplayPinCode(const QDBusObjectPath& device, const QString& pin);
  quint32 RequestPasskey(const QDBusObjectPath& device);
  void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
  void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey);
  void RequestAuthorization(const QDBusObjectPath& device);
  void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
  void Cancel();

 private:
  bool authorized();
  void request(const QDBusObjectPath& device, BluetoothInteraction kind, const QString& code = {},
               const QString& uuid = {}, quint16 entered = 0, bool delayed = true);
  struct Pending {
    BluetoothAgentRequest request;
    QDBusMessage message;
    bool delayed;
  };
  QDBusConnection connection_;
  QString owner_;
  int timeout_ = 120000;
  std::function<QString(const QString&)> resolver_;
  QHash<QString, QString> devices_;
  QHash<QString, Pending> pending_;
};
}  // namespace HoloNight::System
