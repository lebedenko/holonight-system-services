# External window title presentation

Work package: I-001. Baseline: `3d76f6df2006813d5c160745dfe03a6cea4e1b83`.

See the umbrella initiative for settled contracts and scope. Implementation remains local; publication and integration are pending.

## Requirements

Own independently linkable Compositor with private IPC/protocol backends. Preserve activation/workspace behavior. Sway uses valid leaf deco_rect and fullscreen ancestry. Other interfaces remain Unknown. PID/app ID identity is exact and unique. Wayland protocols can be excluded. No Qt provider dependency or daemon.

## Implementation

Public contracts live in `include/holonight_system/compositor`; the independently installed `HoloNightSystem::Compositor` target owns private factories, IPC, backends and protocol bindings. Shared implementations and injected-transport tests moved from shell. Audio and Storage consumer builds remain isolated from Compositor.

Sway observations validate native xdg-shell identity, leaf geometry, layout and complete ancestry. Workspace fullscreen_mode is a compatibility sentinel, not fullscreen evidence. Parent tab/stack rectangles never substitute for leaf geometry. Hyprland, labwc and generic Wayland remain Unknown. Refreshes coalesce; matching requires one PID/app-ID pair. Disabled native implementations retain contracts and conservative fallback.

## Verification — 2026-10-05

- Clean enabled build and `ctest --test-dir build/title-acceptance --output-on-failure`: 6/6 suites passed, including moved backend tests and install-tree consumers.
- Compositor-only native-disabled build and `ctest --test-dir build/title-no-wayland --output-on-failure`: 2/2 passed.
- Focused clang-tidy of factory, Sway observation and presentation fixtures passed; REUSE passed.
- `task check` stops on pre-existing formatting in unchanged `include/holonight_system/audio/AudioBackend.h`; this is not a compositor regression.
- Native Sway/Hyprland behavior remains manual and pending. No publication or pin updates.
