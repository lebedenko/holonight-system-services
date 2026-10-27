# Bluetooth client component

`HoloNightSystem::Bluetooth` is an independently installed Qt Core/DBus static client.
Public headers are under `include/holonight_system/bluetooth`. No QML registration,
translation, icon selection or application policy belongs in this component.

## Contract

BluetoothBackend is injectable. BluetoothController owns adapter/device models and
asynchronous request completion. Requests carry a unique ID, lifetime target ID,
operation and raw error name/message. Consumers display errors and pending state.
Conflicting operations on a target fail asynchronously; CancelPairing remains available.
Optional RSSI and battery values remain unknown when absent.

The private BlueZ backend subscribes before GetManagedObjects, retries snapshots
changed during collection and retires IDs after removal or owner change. Calls use
the unique daemon owner; obsolete replies cannot update a new lifetime. Discovery
ownership is tracked separately from Discovering and releases only this client's
session. Agent1 validates the daemon sender, delays input replies, validates PIN and
passkey input, preserves leading zero display codes, and clears interactions on
cancellation, timeout, target removal and daemon loss. Registration uses
KeyboardDisplay. Default-agent status is opt-in and failures are diagnostics.

## Consumer policy

Applications select the first powered adapter or first adapter in stable order and
retain explicit selection. Explicit pairing chains Pair, Trust, Connect; a later
failure preserves prior successful steps. Incoming authorization does not imply trust.
Discoverability uses a 180-second timeout. No startup power change or rfkill recovery.

## Verification

Focused fake-backend tests cover invalid targets, conflicts and retired requests.
Private-bus tests cover BlueZ snapshots, discovery ownership, invalidation, restart,
and agent protocol behavior. An install-tree consumer verifies component isolation.
Hardware acceptance requires headphones and a keyboard, including concurrent clients.
