#pragma once
#include "StorageTypes.h"

#include <QAbstractListModel>

#include <optional>

namespace HoloNight::System {
class StorageVolumeModel : public QAbstractListModel {
  Q_OBJECT
 public:
  using QAbstractListModel::QAbstractListModel;
  // Qt model roles require implicit integer conversion; preserve their public ABI.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class,performance-enum-size)
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
    CanUnmountRole,
    PartitionNameRole,
    PartitionNumberRole,
    HintNameRole,
    HintIconNameRole,
    HintSymbolicIconNameRole
  };
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] const QList<StorageVolume>& items() const { return items_; }
  [[nodiscard]] std::optional<StorageVolume> find(const QString& identifier) const;
  void apply(const QList<StorageVolume>& items);

 private:
  QList<StorageVolume> items_;
};
}  // namespace HoloNight::System
