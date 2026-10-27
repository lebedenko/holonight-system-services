#include "BluetoothDeviceModel.h"
namespace HoloNight::System {
QVariant BluetoothDeviceModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
  const auto& item = items_[index.row()];
  switch (role) {
    case IdRole:
      return QVariant::fromValue(item.id);
    case AdapterIdRole:
      return QVariant::fromValue(item.adapterId);
    case AddressRole:
      return QVariant::fromValue(item.address);
    case AddressTypeRole:
      return QVariant::fromValue(item.addressType);
    case NameRole:
      return QVariant::fromValue(item.name);
    case AliasRole:
      return QVariant::fromValue(item.alias);
    case IconRole:
      return QVariant::fromValue(item.icon);
    case DeviceClassRole:
      return QVariant::fromValue(item.deviceClass);
    case AppearanceRole:
      return QVariant::fromValue(item.appearance);
    case PairedRole:
      return QVariant::fromValue(item.paired);
    case BondedRole:
      return QVariant::fromValue(item.bonded);
    case TrustedRole:
      return QVariant::fromValue(item.trusted);
    case BlockedRole:
      return QVariant::fromValue(item.blocked);
    case ConnectedRole:
      return QVariant::fromValue(item.connected);
    case RssiRole:
      return item.rssi ? QVariant(*item.rssi) : QVariant();
    case BatteryRole:
      return item.battery ? QVariant(*item.battery) : QVariant();
    case UuidsRole:
      return QVariant::fromValue(item.uuids);
    default:
      return {};
  }
}
QHash<int, QByteArray> BluetoothDeviceModel::roleNames() const {
  return {{IdRole, "id"},
          {AdapterIdRole, "adapterId"},
          {AddressRole, "address"},
          {AddressTypeRole, "addressType"},
          {NameRole, "name"},
          {AliasRole, "alias"},
          {IconRole, "icon"},
          {DeviceClassRole, "deviceClass"},
          {AppearanceRole, "appearance"},
          {PairedRole, "paired"},
          {BondedRole, "bonded"},
          {TrustedRole, "trusted"},
          {BlockedRole, "blocked"},
          {ConnectedRole, "connected"},
          {RssiRole, "rssi"},
          {BatteryRole, "battery"},
          {UuidsRole, "uuids"}};
}
void BluetoothDeviceModel::apply(const QList<BluetoothDevice>& items) {
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
std::optional<BluetoothDevice> BluetoothDeviceModel::find(const QString& id) const {
  for (const auto& item : items_)
    if (item.id == id) return item;
  return {};
}
}  // namespace HoloNight::System
