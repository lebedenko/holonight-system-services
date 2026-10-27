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
  QString id, adapterId, address, addressType, name, alias, icon;
  quint32 deviceClass = 0;
  quint16 appearance = 0;
  bool paired = false, bonded = false, trusted = false, blocked = false, connected = false;
  std::optional<int> rssi, battery;
  QStringList uuids;
  bool operator==(const BluetoothDevice&) const = default;
};
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
  Forget
};
struct BluetoothResult {
  QString requestId, targetId;
  BluetoothOperation operation = BluetoothOperation::Connect;
  QString errorName, errorMessage;
  bool succeeded() const { return errorName.isEmpty(); }
};
enum class BluetoothInteraction {
  PinEntry,
  PinDisplay,
  PasskeyEntry,
  PasskeyDisplay,
  Confirmation,
  PairingAuthorization,
  ServiceAuthorization
};
struct BluetoothAgentRequest {
  QString requestId, deviceId;
  BluetoothInteraction interaction = BluetoothInteraction::Confirmation;
  QString code, serviceUuid;
  quint16 entered = 0;
};
}  // namespace HoloNight::System
Q_DECLARE_METATYPE(HoloNight::System::BluetoothAdapter)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothDevice)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothResult)
Q_DECLARE_METATYPE(HoloNight::System::BluetoothAgentRequest)
