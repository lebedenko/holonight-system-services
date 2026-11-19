#pragma once
#include <QList>
#include <QMetaType>
#include <QStringList>
#include <QVariant>

#include <optional>
namespace HoloNight::System {
struct BluetoothAdapter {
  QString id, address, alias;
  bool powered = false, discoverable = false, discovering = false;
  bool operator==(const BluetoothAdapter&) const = default;
};
struct BluetoothDevice {
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString id, adapterId, address, addressType, name, alias, icon;
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  quint32 deviceClass = 0;
  quint16 appearance = 0;
  bool paired = false, bonded = false, trusted = false, blocked = false, connected = false;
  std::optional<int> rssi, battery;
  QStringList uuids;
  bool operator==(const BluetoothDevice&) const = default;
};
// Keep the existing enum representation stable for consumers.
// NOLINTNEXTLINE(performance-enum-size)
enum class BluetoothOperation {
  Power,
  AdapterAlias,
  Discoverable,
  StartDiscovery,
  StopDiscovery,
  Pair,
  CancelPairing,
  Connect,
  Disconnect,
  DeviceAlias,
  Trust,
  Block,
  Forget,
};
struct BluetoothResult {
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString requestId, targetId;
  BluetoothOperation operation = BluetoothOperation::Connect;
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString errorName, errorMessage;
  [[nodiscard]] bool succeeded() const { return errorName.isEmpty(); }
};
// Keep the existing enum representation stable for consumers.
// NOLINTNEXTLINE(performance-enum-size)
enum class BluetoothInteraction {
  PinEntry,
  PinDisplay,
  PasskeyEntry,
  PasskeyDisplay,
  Confirmation,
  PairingAuthorization,
  ServiceAuthorization,
};
struct BluetoothAgentRequest {
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString requestId, deviceId;
  BluetoothInteraction interaction = BluetoothInteraction::Confirmation;
  // NOLINTNEXTLINE(readability-identifier-naming)  // Published API spelling.
  QString code, serviceUuid;
  quint16 entered = 0;
};
}  // namespace HoloNight::System
Q_DECLARE_METATYPE(HoloNight::System::BluetoothAdapter)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothDevice)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothResult)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothAgentRequest)
