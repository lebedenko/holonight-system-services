#pragma once
#include "StorageBackend.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QMap>
#include <QVariantMap>

namespace HoloNight::System {
using StorageInterfaces = QMap<QString, QVariantMap>;
using StorageObjects = QMap<QDBusObjectPath, StorageInterfaces>;

class UDisks2Backend final : public StorageBackend {
  Q_OBJECT
public:
  explicit UDisks2Backend(QObject *parent = nullptr);
  UDisks2Backend(QDBusConnection connection, QString service,
                 QObject *parent = nullptr);
  void start() override;
  void stop() override;
  void execute(const QString &requestId, StorageOperation operation,
               const QString &targetId) override;
private slots:
  void interfacesAdded(const QDBusObjectPath &path,
                       const HoloNight::System::StorageInterfaces &interfaces);
  void interfacesRemoved(const QDBusObjectPath &path,
                         const QStringList &interfaces);
  void propertiesChanged(const QString &interface, const QVariantMap &changed,
                         const QStringList &invalidated,
                         const QDBusMessage &message);
  void ownerChanged(const QString &service, const QString &oldOwner,
                    const QString &newOwner);

private:
  void setOwner(const QString &owner);
  void refresh();
  void changed();
  void publish(const StorageObjects &objects);
  QString idFor(const QString &path, const QString &interface);
  QDBusConnection connection_;
  QString service_;
  QString owner_;
  bool started_ = false;
  bool refreshing_ = false;
  quint64 generation_ = 0;
  quint64 revision_ = 0;
  QHash<QString, QString> ids_;
  QList<StorageResult> completions_;
};
} // namespace HoloNight::System
Q_DECLARE_METATYPE(HoloNight::System::StorageInterfaces)
Q_DECLARE_METATYPE(HoloNight::System::StorageObjects)
