# HoloNight System Services

Reusable Qt/C++ system integration for independent HoloNight applications. Consumers retain presentation,
QML registration, translations, filtering and application policy.

- `HoloNightSystem::Audio`: PulseAudio integration, controller, device and stream models, and value types.
- `HoloNightSystem::Compositor`: compositor snapshots, activation/workspace contracts and conservative title-bar observations.
- `HoloNightSystem::Storage`: UDisks2 discovery, drive/volume models and asynchronous manual storage operations.

## Build and consume

```sh
cmake -S . -B build/test -G Ninja -DBUILD_TESTS=ON
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

Qt >=6.11 and a C++23 compiler are required. Components are enabled by default. Audio requires libpulse;
Storage requires Qt DBus; Compositor requires Qt Network and, with `BUILD_COMPOSITOR_WAYLAND=ON`,
Qt Gui/WaylandClient and wayland-client. Disable every unwanted component explicitly with `BUILD_AUDIO`,
`BUILD_STORAGE` and `BUILD_COMPOSITOR`; `BUILD_COMPOSITOR_WAYLAND=OFF` keeps IPC-only Compositor.
Tests require Qt Test and installed GTest >=1.14 CONFIG with GTest/GMock targets. Storage tests additionally
require `dbus-run-session`. No private Qt components or internal HoloNight providers are used.

CMake is authoritative. `task configure`, `task build` and `task test` check installed prerequisites without
downloading packages or preparing sources. Run `task deps PRESET=test` explicitly for preparation and
prerequisite checks. Use `HOLONIGHT_EXTERNAL_PREFIX` for an installed external prefix (spaces are supported),
or normal system package locations. Incompatible configured trees are preserved as `build/<preset>.previous-N`.
Use `HOLONIGHT_BUILD_FEATURES="BUILD_AUDIO=OFF BUILD_STORAGE=ON BUILD_COMPOSITOR=OFF"` with Task
to select features; direct CMake uses the corresponding `-D` options.

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


## Local CI rehearsal

Acquire the qualified immutable images explicitly with `task ci:prepare` (acquisition followed by acceptance),
or `python3 scripts/ci/run.py --acquire-images --lane <lane>`. Subsequent `task ci` runs use
`--pull=never --network=none`; absent images fail without acquisition. Docker is preferred, with Podman
used when Docker is absent. The linux/amd64 images require an amd64 host or emulation.

Clean acceptance covers full Debug tests and all install-tree consumers followed by full Release without tests,
Audio only, Storage only, Compositor with and without Wayland, and Core only with all components/tests disabled.
Core-only uses direct CMake and does not run CTest or editor refresh because it has no compilation database.
The static lane runs format and full tidy checks. Licensing retains the separate immutable REUSE image.
Storage backend tests retain their isolated D-Bus session. Every single-component install test also checks that
requests for unavailable components fail.

Tracked edits and non-ignored new files enter read-only snapshots; new inputs are reported to add before
publication. Each lane builds in a disposable writable copy with a fresh tree. Rootless account mapping is
preserved. Complete logs and exit statuses are written under `build/ci/`; any failed lane fails the command.
See [the EDM-005 SDD](docs/sdd/external-dependency-management/SPEC.md) for verification and image identities.

## Compositor contract

Include `<holonight_system/compositor/CompositorFactory.h>` and link `HoloNightSystem::Compositor`. `createCompositorBackend()` selects the backend using desktop tokens before session markers; conflicting evidence uses generic Wayland. Explicit backend selection is available for shell plugins. Concrete backends, transports and protocol bindings remain private. No running shell or daemon is required.

Snapshot window identifiers are opaque. Window inventory retains native PID/app identity, titles and fullscreen state. Activation matching remains independent of application presentation. Applications control their own content headings.

Backend events and reconnects refresh snapshots, with concurrent refreshes coalesced. Disconnections publish unavailable snapshots.

For compositor-only builds use `-DBUILD_AUDIO=OFF -DBUILD_STORAGE=OFF`. `-DBUILD_COMPOSITOR_WAYLAND=OFF` excludes generated Wayland protocol implementations while keeping the API and socket backends; unavailable explicit protocol backends report disconnected snapshots. Wayland implementations link directly to Qt Gui/WaylandClient and wayland-client, without a dependency on holonight-qt.
