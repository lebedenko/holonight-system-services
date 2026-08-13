#include "AudioStreamModel.h"

namespace HoloNight::System {

AudioStreamModel::AudioStreamModel(QObject *parent)
    : QAbstractListModel(parent) {}

int AudioStreamModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(streams_.size());
}

QVariant AudioStreamModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(streams_.size())) {
    return {};
  }
  const AudioStream &stream = streams_.at(index.row());
  switch (static_cast<Role>(role)) {
  case Role::StreamId:
    return stream.id;
  case Role::Name:
    return stream.name;
  case Role::Application:
    return stream.application;
  case Role::IconName:
    return stream.icon_name;
  case Role::Volume:
    return stream.volume;
  case Role::Muted:
    return stream.muted;
  case Role::CurrentDevice:
    return stream.device;
  default:
    return {};
  }
}

QHash<int, QByteArray> AudioStreamModel::roleNames() const {
  static const QHash<int, QByteArray> kRoles{
      {static_cast<int>(Role::StreamId), "streamId"},
      {static_cast<int>(Role::Name), "name"},
      {static_cast<int>(Role::Application), "application"},
      {static_cast<int>(Role::IconName), "iconName"},
      {static_cast<int>(Role::Volume), "volume"},
      {static_cast<int>(Role::Muted), "muted"},
      {static_cast<int>(Role::CurrentDevice), "currentDevice"},
  };
  return kRoles;
}

void AudioStreamModel::applyAdd(const AudioStream &stream) {
  for (int row = 0; row < static_cast<int>(streams_.size()); ++row) {
    if (streams_.at(row).id == stream.id) {
      streams_.replace(row, stream);
      const QModelIndex idx = index(row);
      emit dataChanged(idx, idx);
      return;
    }
  }

  const int row = static_cast<int>(streams_.size());
  beginInsertRows(QModelIndex(), row, row);
  streams_.append(stream);
  endInsertRows();
}

void AudioStreamModel::applyChange(const AudioStream &stream) {
  for (int row = 0; row < static_cast<int>(streams_.size()); ++row) {
    if (streams_.at(row).id == stream.id) {
      streams_.replace(row, stream);
      const QModelIndex idx = index(row);
      emit dataChanged(idx, idx);
      return;
    }
  }
  applyAdd(stream);
}

void AudioStreamModel::applyRemove(uint32_t stream_id) {
  for (int row = 0; row < static_cast<int>(streams_.size()); ++row) {
    if (streams_.at(row).id == stream_id) {
      beginRemoveRows(QModelIndex(), row, row);
      streams_.removeAt(row);
      endRemoveRows();
      return;
    }
  }
}

void AudioStreamModel::clear() {
  beginResetModel();
  streams_.clear();
  endResetModel();
}

} // namespace HoloNight::System
