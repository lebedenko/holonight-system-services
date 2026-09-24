#pragma once

#include "StorageTypes.h"
#include <QObject>

namespace HoloNight::System {

// Backends own ID lifetimes. Replies must be asynchronous, including failures.
// An unavailable snapshot retires all current targets.
class StorageBackend : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void execute(const QString &requestId, StorageOperation operation,
                       const QString &targetId) = 0;
signals:
  void snapshotChanged(QList<StorageDrive> drives, QList<StorageVolume> volumes,
                       bool available);
  void operationFinished(StorageResult result);
};
} // namespace HoloNight::System
