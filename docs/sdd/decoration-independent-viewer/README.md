# Decoration-independent Viewer

Work package: I-003. Exact upstream baseline: `398804a7cce5a57f9f6870c4e7ec99e9b1f3ddaa` (origin/main).

## Requirements and design

Remove decoration contract; preserve compositor behavior. Follow the [umbrella contract](../../../../docs/initiatives/decoration-independent-viewer/README.md). No decoration settings or desktop heuristics. Preserve unrelated behavior.

## Implementation and verification

Removed ExternalTitleBarState, external_title_bar, externalTitleBarForApplication and requestSnapshotRefresh (including backend overrides). Removed Sway leaf decoration geometry and detection-only ancestor layout validation. Preserved positive PID/container validation, inventory/title/fullscreen, workspace projection, activation, backend selection and event/reconnect refresh coalescing.

Replaced the detection suite with compositor inventory/fullscreen/identity/selection regressions. Reconnect tests verify cleared/restored inventory; the refresh regression uses window events and title changes. The install consumer verifies disconnected unavailable snapshots through the remaining public API.

## Verification — 2026-10-08

- Clean Debug build/decoration-independent, Qt 6.12.0: all 6 CTest suites passed with IPC permission, including Audio/Storage/Compositor and all install consumers.
- Clean compositor-only build/decoration-independent-off, BUILD_COMPOSITOR_WAYLAND=OFF: both CTest suites passed.
- Focused inventory regressions, changed-source/test clang-tidy with owned header filters, changed-file clang-format, REUSE and final diff review passed.
- Repository-wide format-check remains blocked by pre-existing formatting in include/holonight_system/audio/AudioBackend.h; that unrelated file was preserved. Initial sandbox IPC/D-Bus failures were resolved by the permitted test rerun. Generated protocol warnings from an unfiltered exploratory tidy invocation were excluded using the repository-owned header filter.
- Logs: /tmp/decoration-services-tests.log, /tmp/decoration-services-off-tests.log, /tmp/decoration-services-{tidy-final,inventory-tidy-corrected,test-tidy-final}.log and /tmp/decoration-services-format.log.

Provider stage: /tmp/holonight-decoration-independent/services, also copied into Shell's single consistent build/decoration-provider prefix. Publication and pinning remain separate.
