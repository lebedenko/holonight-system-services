#pragma once
#include "StorageTypes.h"
#include <QAbstractListModel>
#include <optional>

namespace HoloNight::System {
class StorageDriveModel : public QAbstractListModel {
  Q_OBJECT
public:
  using QAbstractListModel::QAbstractListModel;
  enum Role {
    IdRole = Qt::UserRole + 1,
    VendorRole,
    ModelRole,
    SerialRole,
    ConnectionBusRole,
    SiblingIdRole,
    RemovableRole,
    MediaRemovableRole,
    MediaPresentRole,
    OpticalRole,
    CanEjectRole,
    CanPowerOffRole
  };
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  const QList<StorageDrive> &items() const { return items_; }
  std::optional<StorageDrive> find(const QString &id) const;
  void apply(const QList<StorageDrive> &items);

private:
  QList<StorageDrive> items_;
};
} // namespace HoloNight::System
