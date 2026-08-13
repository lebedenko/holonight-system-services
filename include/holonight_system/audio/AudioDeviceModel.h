#pragma once

#include "AudioTypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariantMap>

#include <cstdint>

namespace HoloNight::System {

class AudioDeviceModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(
      QVariantMap defaultDevice READ defaultDevice NOTIFY defaultDeviceChanged)

public:
  enum class Role : uint16_t {
    DeviceId = Qt::UserRole + 1,
    Name,
    Description,
    DisplayName,
    RawDescription,
    VendorName,
    ProductName,
    PortName,
    PortDescription,
    FormFactor,
    Volume,
    Muted,
    IsDefault,
    BusType,
    ChannelCount,
    SampleRate,
    Codec,
    IconName,
  };

  explicit AudioDeviceModel(QObject *parent = nullptr);

  [[nodiscard]] int
  rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QVariantMap defaultDevice() const { return default_device_; }

  void applyAdd(const AudioDevice &device);
  void applyChange(const AudioDevice &device);
  void applyRemove(uint32_t device_id);
  void clear();

signals:
  void defaultDeviceChanged();

private:
  void refreshDefaultDevice();

  QList<AudioDevice> devices_;
  QVariantMap default_device_;
};

} // namespace HoloNight::System
