#include "StorageVolumeModel.h"

namespace HoloNight::System {
int StorageVolumeModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(items_.size());
}
QVariant StorageVolumeModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.model() != this || index.column() != 0 ||
      index.row() < 0 || index.row() >= items_.size())
    return {};
  const auto &item = items_.at(index.row());
  switch (role) {
  case IdRole:
    return QVariant::fromValue(item.id);
  case DriveIdRole:
    return QVariant::fromValue(item.driveId);
  case LabelRole:
    return QVariant::fromValue(item.label);
  case DeviceRole:
    return QVariant::fromValue(item.device);
  case FilesystemTypeRole:
    return QVariant::fromValue(item.filesystemType);
  case UsageRole:
    return QVariant::fromValue(item.usage);
  case PartitionTypeRole:
    return QVariant::fromValue(item.partitionType);
  case CryptoBackingIdRole:
    return QVariant::fromValue(item.cryptoBackingId);
  case MountPointsRole:
    return QVariant::fromValue(item.mountPoints);
  case CapacityRole:
    return QVariant::fromValue(item.capacity);
  case HintIgnoreRole:
    return QVariant::fromValue(item.hintIgnore);
  case HintSystemRole:
    return QVariant::fromValue(item.hintSystem);
  case LoopRole:
    return QVariant::fromValue(item.loop);
  case PartitionContainerRole:
    return QVariant::fromValue(item.partitionContainer);
  case LockedRole:
    return QVariant::fromValue(item.locked);
  case CanMountRole:
    return QVariant::fromValue(item.canMount);
  case CanUnmountRole:
    return QVariant::fromValue(item.canUnmount);
  default:
    return {};
  }
}
QHash<int, QByteArray> StorageVolumeModel::roleNames() const {
  return {{IdRole, "id"},
          {DriveIdRole, "driveId"},
          {LabelRole, "label"},
          {DeviceRole, "device"},
          {FilesystemTypeRole, "filesystemType"},
          {UsageRole, "usage"},
          {PartitionTypeRole, "partitionType"},
          {CryptoBackingIdRole, "cryptoBackingId"},
          {MountPointsRole, "mountPoints"},
          {CapacityRole, "capacity"},
          {HintIgnoreRole, "hintIgnore"},
          {HintSystemRole, "hintSystem"},
          {LoopRole, "loop"},
          {PartitionContainerRole, "partitionContainer"},
          {LockedRole, "locked"},
          {CanMountRole, "canMount"},
          {CanUnmountRole, "canUnmount"}};
}
std::optional<StorageVolume> StorageVolumeModel::find(const QString &id) const {
  for (const auto &item : items_)
    if (item.id == id)
      return item;
  return std::nullopt;
}
void StorageVolumeModel::apply(const QList<StorageVolume> &items) {
  // Preserve rows and persistent indexes for unchanged identities.
  for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
    bool present = false;
    for (const auto &item : items)
      if (item.id == items_[i].id) {
        present = true;
        break;
      }
    if (!present) {
      beginRemoveRows({}, i, i);
      items_.removeAt(i);
      endRemoveRows();
    }
  }
  for (const auto &item : items) {
    int row = 0;
    while (row < items_.size() && items_[row].id != item.id)
      ++row;
    if (row == items_.size()) {
      beginInsertRows({}, row, row);
      items_.append(item);
      endInsertRows();
    } else if (items_[row] != item) {
      items_[row] = item;
      emit dataChanged(index(row), index(row));
    }
  }
}
} // namespace HoloNight::System
