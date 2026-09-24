# UDisks2 Storage

Approved scope: user-provided Shared UDisks2 storage support plan, 2026-09-24.
Baseline: 60685b9d9f050bfba42b60191a498bd861744f37. Umbrella work package: I-001.

## Requirements

- R1: Expose independently linkable Storage with Core/DBus only; preserve default Audio package consumption.
- R2: Expose normalized drives and volumes, opaque lifetime IDs, full hidden topology and raw visibility hints.
- R3: Discover asynchronously through ObjectManager and refresh after interface/property changes, invalidation and restart.
  Discard stale enumeration and operation replies; IDs must not survive disappearance/reconnection.
- R4: Return request IDs for mount/unmount/eject/power-off; expose busy targets and completion/error signals.
- R5: Unmount affected volumes sequentially before drive removal; stop on any failure. Never force or retry operations.
- R6: Power-off scope includes sibling drives and their volumes and must match the consumer-confirmed scope.
- R7: Preserve upstream authorization, busy, cancellation and disappearance failures without optimistic success.
- R8: Keep presentation, registration, translations and navigation in consumers.

## Design

StorageBackend is constructor-injected and emits complete typed snapshots and operation results. UDisks2Backend
is private and accepts a bus connection/service name for isolated tests. Its ObjectManager snapshot requests are
coalesced; a signal revision and connection generation prevent late snapshots overwriting newer state. Interfaces
removed retire IDs immediately. Reconnection retires all IDs. No filesystem UUID or row is an operation target.

Separate QAbstractListModels expose typed records and roles. StorageController owns operation coordination,
locks overlapping drive scopes, and validates targets before each asynchronous step. Power-off takes the exact
scope shown by the consumer, rejecting topology changes. Upstream is authoritative across processes.

## Verification

Verified 2026-09-24 with GCC 16.2.1, Qt 6.11.2 and libpulse 17.0:

- Clean configure: `cmake -S holonight-system-services -B /tmp/holonight-storage-acceptance -G Ninja -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`.
- Clean build: `cmake --build /tmp/holonight-storage-acceptance -j 4`; all 28 steps passed without warnings.
- `ctest --test-dir /tmp/holonight-storage-acceptance --output-on-failure`: 4/4 suites passed,
  including 84 existing Audio tests, 19 Storage tests, legacy Audio and Storage-only install-tree consumers.
- After the final explicit interactive-authorization flag correction: incremental build and
  `ctest --test-dir /tmp/holonight-storage-acceptance -R storage --output-on-failure` passed 2/2 affected suites.
- Storage install consumption disables PkgConfig discovery and rejects imported Audio/libpulse targets.
- `clang-format --dry-run --Werror` passed for new C++ sources; `git diff --check` passed.
- `reuse lint` passed (44/44 files); sandbox blocked its multiprocessing socket on the first attempt,
  so the successful run used an approved unsandboxed retry.

Coverage includes full hidden topology, stable model identity, empty readers, external SSD removability facts,
locked/cleartext association, partial unmount failure, remount interference, sibling scope changes, busy conflict,
authorization cancellation, no force options, isolated D-Bus invalidation/hotplug, stale enumeration and operation
replies, restart identity retirement, mount-path completion, and independent-controller convergence.
Consumer visibility policy and navigation checks are deliberately owned by I-002/I-003 and remain pending.
Manual hardware/polkit checks belong to final ecosystem acceptance and are not simulated as successful here.
