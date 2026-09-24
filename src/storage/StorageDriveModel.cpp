#include "StorageDriveModel.h"

namespace HoloNight::System {
int StorageDriveModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(items_.size());
}
QVariant StorageDriveModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.model() != this || index.column() != 0 ||
      index.row() < 0 || index.row() >= items_.size())
    return {};
  const auto &item = items_.at(index.row());
  switch (role) {
  case IdRole:
    return QVariant::fromValue(item.id);
  case VendorRole:
    return QVariant::fromValue(item.vendor);
  case ModelRole:
    return QVariant::fromValue(item.model);
  case SerialRole:
    return QVariant::fromValue(item.serial);
  case ConnectionBusRole:
    return QVariant::fromValue(item.connectionBus);
  case SiblingIdRole:
    return QVariant::fromValue(item.siblingId);
  case RemovableRole:
    return QVariant::fromValue(item.removable);
  case MediaRemovableRole:
    return QVariant::fromValue(item.mediaRemovable);
  case MediaPresentRole:
    return QVariant::fromValue(item.mediaPresent);
  case OpticalRole:
    return QVariant::fromValue(item.optical);
  case CanEjectRole:
    return QVariant::fromValue(item.canEject);
  case CanPowerOffRole:
    return QVariant::fromValue(item.canPowerOff);
  default:
    return {};
  }
}
QHash<int, QByteArray> StorageDriveModel::roleNames() const {
  return {{IdRole, "id"},
          {VendorRole, "vendor"},
          {ModelRole, "model"},
          {SerialRole, "serial"},
          {ConnectionBusRole, "connectionBus"},
          {SiblingIdRole, "siblingId"},
          {RemovableRole, "removable"},
          {MediaRemovableRole, "mediaRemovable"},
          {MediaPresentRole, "mediaPresent"},
          {OpticalRole, "optical"},
          {CanEjectRole, "canEject"},
          {CanPowerOffRole, "canPowerOff"}};
}
std::optional<StorageDrive> StorageDriveModel::find(const QString &id) const {
  for (const auto &item : items_)
    if (item.id == id)
      return item;
  return std::nullopt;
}
void StorageDriveModel::apply(const QList<StorageDrive> &items) {
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
