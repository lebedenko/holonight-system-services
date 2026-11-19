#pragma once
#include "BluetoothTypes.h"

#include <QAbstractListModel>
namespace HoloNight::System {
class BluetoothAdapterModel : public QAbstractListModel {
  Q_OBJECT
 public:
  using QAbstractListModel::QAbstractListModel;
  // Qt role constants retain their class scope and representation for consumers.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class,performance-enum-size)
  enum Role {
    IdRole = Qt::UserRole + 1,
    AddressRole = Qt::UserRole + 2,
    AliasRole = Qt::UserRole + 3,
    PoweredRole = Qt::UserRole + 4,
    DiscoverableRole = Qt::UserRole + 5,
    DiscoveringRole = Qt::UserRole + 6,
  };
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
  }
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  void apply(const QList<BluetoothAdapter>& items);
  [[nodiscard]] const QList<BluetoothAdapter>& items() const { return items_; }
  [[nodiscard]] std::optional<BluetoothAdapter> find(const QString& identifier) const;

 private:
  QList<BluetoothAdapter> items_;
};
}  // namespace HoloNight::System
