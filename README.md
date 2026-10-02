# HoloNight System Services

Reusable Qt/C++ system integration for independent HoloNight applications. Consumers retain presentation,
QML registration, translations, filtering and application policy.

- `HoloNightSystem::Audio`: PulseAudio integration, controller, device and stream models, and value types.
- `HoloNightSystem::Storage`: UDisks2 discovery, drive/volume models and asynchronous manual storage operations.

## Build and consume

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTS=ON
cmake --build build/debug
ctest --test-dir build/test --output-on-failure
```

Components are enabled by default. `-DBUILD_AUDIO=OFF` builds Storage without libpulse; `-DBUILD_STORAGE=OFF`
builds Audio without Qt DBus. Tests additionally require Qt Test, GTest and, for Storage, `dbus-run-session`.

```cmake
find_package(HoloNightSystemServices REQUIRED COMPONENTS Storage)
target_link_libraries(my_application PRIVATE HoloNightSystem::Storage)
```

Storage requires Qt Core/DBus only. Request both `Audio Storage` to use both components. Existing package
consumers without `COMPONENTS` continue to load Audio and its libpulse dependency.

## Storage contract

Include `StorageController.h` and create an application-owned `HoloNight::System::StorageController`.
For tests, inject a `StorageBackend` that outlives the controller. Injected backends are not shared between
controllers; each controller starts/stops its backend. No QML types are registered by the library.

`drives()` and `volumes()` expose separate list models and typed records. IDs are opaque and valid only for the
current device lifetime. Use IDs, never rows, object paths or UUIDs, for operations. Keep hidden volumes internally:
`HintIgnore`, `HintSystem`, partition purpose, loop/container/crypto facts and mount paths are consumer filter inputs.
`HintSystem` is an authorization hint, not a hide flag. `removable` includes external SSDs independently of
`mediaRemovable`. Cleartext volumes inherit their backing drive association.

For a display name, prefer the volume's nonempty `hintName`, then filesystem `label`, then a meaningful
`partitionName`. Treat generic partition names such as “Basic data partition” as weak metadata. If none is useful,
format `capacity` into a localized name such as “249 GB Volume”; use `device` (for example `/dev/sdb1`) for
technical details or only as a final fallback. `partitionNumber` is available for context when a partition name
needs disambiguation. The library leaves name selection and translation to consumers.

For icons, use `hintIconName` or `hintSymbolicIconName` when the selected theme supplies the requested icon.
Otherwise, use drive `media`, `mediaCompatibility`, `optical`, `removable` and `mediaRemovable` to choose a
standard optical, flash, removable drive or hard disk icon. Consumers own theme lookup and visual style; the
library provides the raw UDisks facts. See the [Icon Naming Specification](https://specifications.freedesktop.org/icon-naming/latest/).

`mount`, `unmount`, `eject` and `powerOff` return request IDs immediately; `operationFinished(StorageResult)`
reports completion, raw error names/messages and the mount path. Upstream state is refreshed before reporting
completion. Consumers translate errors and navigation explanations. `availableChanged` and
`operationStateChanged` notify availability and changes to `busy(targetId)`.

Before power-off, obtain `removalScope(driveId, true)`, display the affected drives and volumes, and pass that
exact scope to `powerOff(driveId, confirmedScope)`. Changed topology invalidates confirmation. Eject targets only
its drive; power-off also affects matching nonempty sibling IDs. Both unmount every mounted affected volume
sequentially, stop on failure, and reject a newly mounted volume before the final operation. They never force or
lazily unmount. Conflicting local requests fail with Busy; unrelated drives may operate concurrently. Local busy
state is not a cross-process lock. UDisks2 results remain authoritative.

## Runtime requirements

Storage consumers require UDisks2 (`udisks2` on Arch Linux), system D-Bus and an existing polkit authentication
agent for operations requiring authorization. Discovery can activate the installed D-Bus service. The library
does not provide an agent, escalate privileges itself, auto-mount media, unlock encrypted storage or retry failed
operations. A missing daemon leaves Storage unavailable; applications remain independent.

The private backend uses the upstream [Drive API](https://storaged.org/udisks/docs/gdbus-org.freedesktop.UDisks2.Drive.html),
[Block API](https://storaged.org/udisks/docs/gdbus-org.freedesktop.UDisks2.Block.html) and
[Partition API](https://storaged.org/udisks/docs/gdbus-org.freedesktop.UDisks2.Partition.html).
See [the local SDD](docs/sdd/udisks2-storage/README.md) for acceptance evidence and pending ecosystem checks.

## Standalone developer tooling

See [tooling/README.md](tooling/README.md) for presets, local dependency overrides, editor refresh,
`task tooling:doctor`, and the independent Serena project.
