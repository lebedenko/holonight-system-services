#pragma once
#include "StorageTypes.h"
#include <QAbstractListModel>
#include <optional>

namespace HoloNight::System {
class StorageVolumeModel : public QAbstractListModel {
  Q_OBJECT
public:
  using QAbstractListModel::QAbstractListModel;
  enum Role {
    IdRole = Qt::UserRole + 1,
    DriveIdRole,
    LabelRole,
    DeviceRole,
    FilesystemTypeRole,
    UsageRole,
    PartitionTypeRole,
    CryptoBackingIdRole,
    MountPointsRole,
    CapacityRole,
    HintIgnoreRole,
    HintSystemRole,
    LoopRole,
    PartitionContainerRole,
    LockedRole,
    CanMountRole,
    CanUnmountRole
  };
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  const QList<StorageVolume> &items() const { return items_; }
  std::optional<StorageVolume> find(const QString &id) const;
  void apply(const QList<StorageVolume> &items);

private:
  QList<StorageVolume> items_;
};
} // namespace HoloNight::System
