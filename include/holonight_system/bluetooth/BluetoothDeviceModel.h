#pragma once
#include "BluetoothTypes.h"

#include <QAbstractListModel>
namespace HoloNight::System {
class BluetoothDeviceModel : public QAbstractListModel {
  Q_OBJECT
 public:
  using QAbstractListModel::QAbstractListModel;
  enum Role {
    IdRole = Qt::UserRole + 1,
    AdapterIdRole = Qt::UserRole + 2,
    AddressRole = Qt::UserRole + 3,
    AddressTypeRole = Qt::UserRole + 4,
    NameRole = Qt::UserRole + 5,
    AliasRole = Qt::UserRole + 6,
    IconRole = Qt::UserRole + 7,
    DeviceClassRole = Qt::UserRole + 8,
    AppearanceRole = Qt::UserRole + 9,
    PairedRole = Qt::UserRole + 10,
    BondedRole = Qt::UserRole + 11,
    TrustedRole = Qt::UserRole + 12,
    BlockedRole = Qt::UserRole + 13,
    ConnectedRole = Qt::UserRole + 14,
    RssiRole = Qt::UserRole + 15,
    BatteryRole = Qt::UserRole + 16,
    UuidsRole = Qt::UserRole + 17
  };
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : items_.size(); }
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void apply(const QList<BluetoothDevice>& items);
  const QList<BluetoothDevice>& items() const { return items_; }
  std::optional<BluetoothDevice> find(const QString& id) const;

 private:
  QList<BluetoothDevice> items_;
};
}  // namespace HoloNight::System
