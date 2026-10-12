#pragma once
#include "StorageTypes.h"

#include <QAbstractListModel>

#include <optional>

namespace HoloNight::System {
class StorageDriveModel : public QAbstractListModel {
  Q_OBJECT
 public:
  using QAbstractListModel::QAbstractListModel;
  // Qt model roles require implicit integer conversion; preserve their public ABI.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class,performance-enum-size)
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
    CanPowerOffRole,
    MediaRole,
    MediaCompatibilityRole,
    RotationRateRole
  };
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] const QList<StorageDrive>& items() const { return items_; }
  [[nodiscard]] std::optional<StorageDrive> find(const QString& identifier) const;
  void apply(const QList<StorageDrive>& items);

 private:
  QList<StorageDrive> items_;
};
}  // namespace HoloNight::System
