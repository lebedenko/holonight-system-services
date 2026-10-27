#pragma once
#include "BlueZAgent.h"
#include "BluetoothBackend.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QVariantMap>
namespace HoloNight::System {
using BluetoothInterfaces = QMap<QString, QVariantMap>;
using BluetoothObjects = QMap<QDBusObjectPath, BluetoothInterfaces>;
class BlueZBackend final : public BluetoothBackend {
  Q_OBJECT
 public:
  explicit BlueZBackend(QObject* parent = nullptr);
  ~BlueZBackend() override { stop(); }
  BlueZBackend(QDBusConnection connection, QString service, QObject* parent = nullptr);
  void start() override;
  void stop() override;
  void execute(const QString& requestId, BluetoothOperation operation, const QString& targetId,
               const QVariant& value) override;
  void respond(const QString& requestId, bool accept, const QString& input) override;
  void requestDefaultAgent() override;
 private slots:
  void interfacesAdded(const QDBusObjectPath& path, const HoloNight::System::BluetoothInterfaces& interfaces,
                       const QDBusMessage& message);
  void interfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces, const QDBusMessage& message);
  void propertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                         const QDBusMessage& message);

 private:
  void setOwner(const QString& owner);
  void changed();
  void refresh();
  void publish(const BluetoothObjects& objects);
  void registerAgent();
  QString idFor(const QString& path);
  QDBusConnection connection_;
  QString service_, owner_, agent_path_;
  BlueZAgent agent_;
  bool started_ = false, refreshing_ = false, default_agent_ = false, registered_ = false;
  quint64 generation_ = 0, revision_ = 0;
  QHash<QString, QString> ids_, device_adapters_;
  QSet<QString> adapters_, discovery_owned_, discovery_pending_;
};
}  // namespace HoloNight::System
Q_DECLARE_METATYPE(HoloNight::System::BluetoothInterfaces)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothObjects)
