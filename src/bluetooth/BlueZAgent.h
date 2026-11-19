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
  void respond(const QString& requestId, bool accept, const QString& input);
  void clear();
 signals:
  void requested(BluetoothAgentRequest request);
  void canceled(QString requestId);
 public slots:
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void Release();
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString RequestPinCode(const QDBusObjectPath& device);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void DisplayPinCode(const QDBusObjectPath& device, const QString& pin);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  quint32 RequestPasskey(const QDBusObjectPath& device);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void RequestAuthorization(const QDBusObjectPath& device);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  void Cancel();

 private:
  bool authorized();
  void request(const QDBusObjectPath& device, BluetoothInteraction kind, const QString& code = {},
               const QString& uuid = {}, quint16 entered = 0, bool delayed = true);
  struct Pending {
    BluetoothAgentRequest request;
    QDBusMessage message;
    bool delayed = false;
  };
  QDBusConnection connection_;
  QString owner_;
  int timeout_ = 120000;
  std::function<QString(const QString&)> resolver_;
  QHash<QString, QString> devices_;
  QHash<QString, Pending> pending_;
};
}  // namespace HoloNight::System
