# Shared System Services

## Specification

The repository exports independently linkable static CMake components under the `HoloNightSystem` namespace.
Public C++ types use `HoloNight::System`. Shared code contains domain controllers, value types, models, backend
interfaces, and private production backends; QML registration and presentation remain consumer-owned.

The initial `HoloNightSystem::Audio` component preserves Shell's PulseAudio behavior: availability and health,
volume and mute, default input/output, device and stream models, stream routing, input-level monitoring, reconnect
backoff, and metadata conversion. `AudioController` accepts an owned `AudioBackend` for deterministic tests.

## Design

`AudioController` owns four Qt list models and an injected backend. The default constructor supplies the private
libpulse backend. Backend events update controller state and models; control calls are forwarded upstream. No
client process is authoritative, so independent clients converge through PulseAudio events.

The installed package discovers only Qt Core, PkgConfig, and libpulse for Audio. NetworkManager and BlueZ are not
dependencies of an Audio consumer.

## Tasks

- [x] Export `HoloNightSystem::Audio` as a static package component.
- [x] Extract controller, models, types, PulseAudio backend, reconnect behavior, and conversion logic.
- [x] Add the constructor-injected `AudioBackend` interface.
- [x] Port the Shell Audio test suite.
- [x] Add an install-tree Audio consumer test.
- [ ] Add Network after Audio consumers are integrated.
- [ ] Add Bluetooth after Audio consumers are integrated.
