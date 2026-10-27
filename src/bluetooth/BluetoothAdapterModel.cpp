#include "BluetoothAdapterModel.h"
namespace HoloNight::System {
QVariant BluetoothAdapterModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
  const auto& item = items_[index.row()];
  switch (role) {
    case IdRole:
      return QVariant::fromValue(item.id);
    case AddressRole:
      return QVariant::fromValue(item.address);
    case AliasRole:
      return QVariant::fromValue(item.alias);
    case PoweredRole:
      return QVariant::fromValue(item.powered);
    case DiscoverableRole:
      return QVariant::fromValue(item.discoverable);
    case DiscoveringRole:
      return QVariant::fromValue(item.discovering);
    default:
      return {};
  }
}
QHash<int, QByteArray> BluetoothAdapterModel::roleNames() const {
  return {{IdRole, "id"},           {AddressRole, "address"},           {AliasRole, "alias"},
          {PoweredRole, "powered"}, {DiscoverableRole, "discoverable"}, {DiscoveringRole, "discovering"}};
}
void BluetoothAdapterModel::apply(const QList<BluetoothAdapter>& items) {
  if (items_ == items) return;
  // Preserve delegate identity for property-only changes.
  bool same = items_.size() == items.size();
  for (int i = 0; same && i < items.size(); ++i) same = items_[i].id == items[i].id;
  if (same) {
    items_ = items;
    if (!items.isEmpty()) emit dataChanged(index(0), index(items.size() - 1));
  } else {
    beginResetModel();
    items_ = items;
    endResetModel();
  }
}
std::optional<BluetoothAdapter> BluetoothAdapterModel::find(const QString& id) const {
  for (const auto& item : items_)
    if (item.id == id) return item;
  return {};
}
}  // namespace HoloNight::System
