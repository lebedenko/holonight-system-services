#pragma once

#include <QList>
#include <QMetaType>
#include <QStringList>

namespace HoloNight::System {

struct StorageDrive {
  QString id;
  QString vendor;
  QString model;
  QString serial;
  QString connectionBus;
  QString siblingId;
  bool removable = false;
  bool mediaRemovable = false;
  bool mediaPresent = false;
  bool optical = false;
  bool canEject = false;
  bool canPowerOff = false;
  bool operator==(const StorageDrive &) const = default;
};

struct StorageVolume {
  QString id;
  QString driveId;
  QString label;
  QString device;
  QString filesystemType;
  QString usage;
  QString partitionType;
  QString cryptoBackingId;
  QStringList mountPoints;
  quint64 capacity = 0;
  bool hintIgnore = false;
  bool hintSystem = false;
  bool loop = false;
  bool partitionContainer = false;
  bool locked = false;
  bool canMount = false;
  bool canUnmount = false;
  bool operator==(const StorageVolume &) const = default;
};

enum class StorageOperation { Mount, Unmount, Eject, PowerOff };
struct StorageResult {
  QString requestId;
  QString targetId;
  StorageOperation operation = StorageOperation::Mount;
  QString mountPath;
  QString errorName;
  QString errorMessage;
  [[nodiscard]] bool succeeded() const { return errorName.isEmpty(); }
};

} // namespace HoloNight::System
Q_DECLARE_METATYPE(HoloNight::System::StorageDrive)
Q_DECLARE_METATYPE(HoloNight::System::StorageVolume)
Q_DECLARE_METATYPE(HoloNight::System::StorageResult)
