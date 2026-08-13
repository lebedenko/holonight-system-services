#pragma once

#include "AudioTypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include <cstdint>

namespace HoloNight::System {

class AudioStreamModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum class Role : uint16_t {
    StreamId = Qt::UserRole + 1,
    Name,
    Application,
    IconName,
    Volume,
    Muted,
    CurrentDevice,
  };

  explicit AudioStreamModel(QObject *parent = nullptr);

  [[nodiscard]] int
  rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void applyAdd(const AudioStream &stream);
  void applyChange(const AudioStream &stream);
  void applyRemove(uint32_t stream_id);
  void clear();

private:
  QList<AudioStream> streams_;
};

} // namespace HoloNight::System
