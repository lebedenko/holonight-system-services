#pragma once
#include "BluetoothTypes.h"

#include <QAbstractListModel>
namespace HoloNight::System {
class BluetoothAdapterModel : public QAbstractListModel {
  Q_OBJECT
 public:
  using QAbstractListModel::QAbstractListModel;
  enum Role {
    IdRole = Qt::UserRole + 1,
    AddressRole = Qt::UserRole + 2,
    AliasRole = Qt::UserRole + 3,
    PoweredRole = Qt::UserRole + 4,
    DiscoverableRole = Qt::UserRole + 5,
    DiscoveringRole = Qt::UserRole + 6
  };
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : items_.size(); }
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void apply(const QList<BluetoothAdapter>& items);
  const QList<BluetoothAdapter>& items() const { return items_; }
  std::optional<BluetoothAdapter> find(const QString& id) const;

 private:
  QList<BluetoothAdapter> items_;
};
}  // namespace HoloNight::System
