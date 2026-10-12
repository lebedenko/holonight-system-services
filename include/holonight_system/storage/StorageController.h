#pragma once

#include "StorageBackend.h"
#include "StorageDriveModel.h"
#include "StorageVolumeModel.h"

#include <QHash>
#include <QSet>

namespace HoloNight::System {

class StorageController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
  Q_PROPERTY(StorageDriveModel* drives READ drives CONSTANT)
  Q_PROPERTY(StorageVolumeModel* volumes READ volumes CONSTANT)
 public:
  explicit StorageController(QObject* parent = nullptr);
  // Backend must outlive the controller; ownership is retained by the caller.
  explicit StorageController(StorageBackend* backend, QObject* parent = nullptr);
  ~StorageController() override;
  StorageController(const StorageController&) = delete;
  StorageController& operator=(const StorageController&) = delete;
  StorageController(StorageController&&) = delete;
  StorageController& operator=(StorageController&&) = delete;
  [[nodiscard]] bool available() const { return available_; }
  StorageDriveModel* drives() { return &drives_; }
  StorageVolumeModel* volumes() { return &volumes_; }
  [[nodiscard]] bool busy(const QString& targetId) const;
  // Sorted lifetime IDs of every affected drive and volume, including hidden
  // volumes.
  [[nodiscard]] QStringList removalScope(const QString& driveId, bool powerOff) const;
  QString mount(const QString& volumeId);
  QString unmount(const QString& volumeId);
  QString eject(const QString& driveId);
  // Pass the scope presented to the user. A changed or omitted scope is
  // rejected.
  QString powerOff(const QString& driveId, const QStringList& confirmedScope);
 signals:
  void availableChanged();
  void operationStateChanged();
  void operationFinished(StorageResult result);

 private:
  struct Request {
    StorageResult result;
    QStringList scope;
    QList<QString> unmounts;
    QString step_id;
    bool final_step = false;
  };
  QString begin(StorageOperation operation, const QString& targetId, const QStringList& confirmedScope = {});
  void advance(const QString& requestId);
  void finish(const QString& requestId, const QString& errorName = {}, const QString& errorMessage = {},
              const QString& mountPath = {});
  void snapshot(const QList<StorageDrive>& drives, const QList<StorageVolume>& volumes, bool available);
  StorageBackend* backend_;
  StorageDriveModel drives_;
  StorageVolumeModel volumes_;
  bool available_ = false;
  QHash<QString, Request> requests_;
};
}  // namespace HoloNight::System
