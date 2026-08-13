#include "AudioDeviceModel.h"

namespace HoloNight::System {

namespace {
QList<int> changedRoles(const AudioDevice &previous,
                        const AudioDevice &current) {
  QList<int> roles;
  const auto append_if_changed = [&roles](bool changed,
                                          AudioDeviceModel::Role role) {
    if (changed) {
      roles.append(static_cast<int>(role));
    }
  };
  append_if_changed(previous.id != current.id,
                    AudioDeviceModel::Role::DeviceId);
  append_if_changed(previous.name != current.name,
                    AudioDeviceModel::Role::Name);
  append_if_changed(previous.description != current.description,
                    AudioDeviceModel::Role::Description);
  append_if_changed(previous.display_name != current.display_name,
                    AudioDeviceModel::Role::DisplayName);
  append_if_changed(previous.raw_description != current.raw_description,
                    AudioDeviceModel::Role::RawDescription);
  append_if_changed(previous.vendor_name != current.vendor_name,
                    AudioDeviceModel::Role::VendorName);
  append_if_changed(previous.product_name != current.product_name,
                    AudioDeviceModel::Role::ProductName);
  append_if_changed(previous.port_name != current.port_name,
                    AudioDeviceModel::Role::PortName);
  append_if_changed(previous.port_description != current.port_description,
                    AudioDeviceModel::Role::PortDescription);
  append_if_changed(previous.form_factor != current.form_factor,
                    AudioDeviceModel::Role::FormFactor);
  append_if_changed(previous.volume != current.volume,
                    AudioDeviceModel::Role::Volume);
  append_if_changed(previous.muted != current.muted,
                    AudioDeviceModel::Role::Muted);
  append_if_changed(previous.is_default != current.is_default,
                    AudioDeviceModel::Role::IsDefault);
  append_if_changed(previous.bus_type != current.bus_type,
                    AudioDeviceModel::Role::BusType);
  append_if_changed(previous.channel_count != current.channel_count,
                    AudioDeviceModel::Role::ChannelCount);
  append_if_changed(previous.sample_rate != current.sample_rate,
                    AudioDeviceModel::Role::SampleRate);
  append_if_changed(previous.codec != current.codec,
                    AudioDeviceModel::Role::Codec);
  append_if_changed(previous.icon_name != current.icon_name,
                    AudioDeviceModel::Role::IconName);
  return roles;
}
} // namespace

AudioDeviceModel::AudioDeviceModel(QObject *parent)
    : QAbstractListModel(parent) {}

int AudioDeviceModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(devices_.size());
}

QVariant AudioDeviceModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(devices_.size())) {
    return {};
  }
  const AudioDevice &dev = devices_.at(index.row());
  switch (static_cast<Role>(role)) {
  case Role::DeviceId:
    return dev.id;
  case Role::Name:
    return dev.name;
  case Role::Description:
    return dev.description;
  case Role::DisplayName:
    return dev.display_name;
  case Role::RawDescription:
    return dev.raw_description;
  case Role::VendorName:
    return dev.vendor_name;
  case Role::ProductName:
    return dev.product_name;
  case Role::PortName:
    return dev.port_name;
  case Role::PortDescription:
    return dev.port_description;
  case Role::FormFactor:
    return dev.form_factor;
  case Role::Volume:
    return dev.volume;
  case Role::Muted:
    return dev.muted;
  case Role::IsDefault:
    return dev.is_default;
  case Role::BusType:
    return dev.bus_type;
  case Role::ChannelCount:
    return dev.channel_count;
  case Role::SampleRate:
    return dev.sample_rate;
  case Role::Codec:
    return dev.codec;
  case Role::IconName:
    return dev.icon_name;
  default:
    return {};
  }
}

QHash<int, QByteArray> AudioDeviceModel::roleNames() const {
  static const QHash<int, QByteArray> kRoles{
      {static_cast<int>(Role::DeviceId), "deviceId"},
      {static_cast<int>(Role::Name), "name"},
      {static_cast<int>(Role::Description), "description"},
      {static_cast<int>(Role::DisplayName), "displayName"},
      {static_cast<int>(Role::RawDescription), "rawDescription"},
      {static_cast<int>(Role::VendorName), "vendorName"},
      {static_cast<int>(Role::ProductName), "productName"},
      {static_cast<int>(Role::PortName), "portName"},
      {static_cast<int>(Role::PortDescription), "portDescription"},
      {static_cast<int>(Role::FormFactor), "formFactor"},
      {static_cast<int>(Role::Volume), "volume"},
      {static_cast<int>(Role::Muted), "muted"},
      {static_cast<int>(Role::IsDefault), "isDefault"},
      {static_cast<int>(Role::BusType), "busType"},
      {static_cast<int>(Role::ChannelCount), "channelCount"},
      {static_cast<int>(Role::SampleRate), "sampleRate"},
      {static_cast<int>(Role::Codec), "codec"},
      {static_cast<int>(Role::IconName), "iconName"},
  };
  return kRoles;
}

void AudioDeviceModel::applyAdd(const AudioDevice &device) {
  for (int row = 0; row < static_cast<int>(devices_.size()); ++row) {
    if (devices_.at(row).id == device.id) {
      const QList<int> roles = changedRoles(devices_.at(row), device);
      if (roles.isEmpty()) {
        return;
      }
      devices_.replace(row, device);
      const QModelIndex idx = index(row);
      emit dataChanged(idx, idx, roles);
      refreshDefaultDevice();
      return;
    }
  }

  const int row = static_cast<int>(devices_.size());
  beginInsertRows(QModelIndex(), row, row);
  devices_.append(device);
  endInsertRows();
  refreshDefaultDevice();
}

void AudioDeviceModel::applyChange(const AudioDevice &device) {
  for (int row = 0; row < static_cast<int>(devices_.size()); ++row) {
    if (devices_.at(row).id == device.id) {
      const QList<int> roles = changedRoles(devices_.at(row), device);
      if (roles.isEmpty()) {
        return;
      }
      devices_.replace(row, device);
      const QModelIndex idx = index(row);
      emit dataChanged(idx, idx, roles);
      refreshDefaultDevice();
      return;
    }
  }
  applyAdd(device);
}

void AudioDeviceModel::applyRemove(uint32_t device_id) {
  for (int row = 0; row < static_cast<int>(devices_.size()); ++row) {
    if (devices_.at(row).id == device_id) {
      beginRemoveRows(QModelIndex(), row, row);
      devices_.removeAt(row);
      endRemoveRows();
      refreshDefaultDevice();
      return;
    }
  }
}

void AudioDeviceModel::clear() {
  beginResetModel();
  devices_.clear();
  endResetModel();
  refreshDefaultDevice();
}

void AudioDeviceModel::refreshDefaultDevice() {
  QVariantMap next;
  for (const AudioDevice &dev : devices_) {
    if (!dev.is_default) {
      continue;
    }
    next = QVariantMap{
        {"deviceId", dev.id},
        {"name", dev.name},
        {"description", dev.description},
        {"displayName", dev.display_name},
        {"rawDescription", dev.raw_description},
        {"vendorName", dev.vendor_name},
        {"productName", dev.product_name},
        {"portName", dev.port_name},
        {"portDescription", dev.port_description},
        {"formFactor", dev.form_factor},
        {"volume", dev.volume},
        {"muted", dev.muted},
        {"isDefault", dev.is_default},
        {"busType", dev.bus_type},
        {"channelCount", dev.channel_count},
        {"sampleRate", dev.sample_rate},
        {"codec", dev.codec},
        {"iconName", dev.icon_name},
    };
    break;
  }

  if (next == default_device_) {
    return;
  }
  default_device_ = next;
  emit defaultDeviceChanged();
}

} // namespace HoloNight::System
