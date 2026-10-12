# External dependency management — holonight-system-services

Work package: EDM-005
Audit baseline: `39472e6dcafc93acea218a234213c346be256586`
Implementation baseline: `3bd6ebca0c0e5f51768105fa1970007e3e83da65` (published `origin/main`, verified before editing)
Umbrella audit: `38146d8f474317f3f8d72699f60e6d16a71dd165`
Status: Done locally; implementation and acceptance complete, publication and umbrella pin approval pending.

## Requirements

- Configuration and normal build commands shall never acquire dependencies or prepare source providers.
- `task deps` shall explicitly prepare supplied local providers and check installed external prerequisites.
- Internal providers shall resolve only from approved prefixes; external packages shall resolve from an explicit
  external prefix or normal system locations.
- Enabled dependency-dependent features shall require their dependencies; minimal profiles shall disable them explicitly.
- Where production components use Qt, discovery shall require >=6.11; private Qt components shall match
  the selected public version exactly.
- Where tests use GTest, consumers shall require installed GTest >=1.14 CONFIG and any GMock targets they use.
- Existing public APIs, component switches, install-tree consumers and acceptance coverage shall be preserved.

## Design

Adopt canonical `tooling/modules/holonight-system-services.json` and `.cmake`, then synchronize bundle 2.0.0 into this repository.
The copied metadata is owned by this standalone checkout. CMake performs authoritative discovery; shared probes
provide prerequisite diagnostics and record resolved versions/paths. Incompatible caches and local provider install
trees are archived before reuse. Package acquisition occurs only in explicit environment preparation. CI switches
build/runtime/release lanes together to qualified immutable images and runs acceptance offline after preparation.

Preserve existing dependency interfaces and component switches; adopt installed GTest CONFIG discovery where tests use it.

## Implementation and verification tasks

- [x] Confirm the exact baseline and canonical handoff before implementation.
- [x] Adopt declarations, prefix helpers, explicit preparation and compatibility signatures.
- [x] Apply the repository-specific discovery changes above.
- [x] Update Task, CI preparation/entrypoints, Dockerfiles, release lanes and contributor instructions.
- [x] Run declaration/CMake/profile consistency checks and offline missing-package regressions.
- [x] Run narrow regressions, clean acceptance, installed-package consumers and required quality checks.
- [x] Inspect complete logs; record commands, versions, features, warnings and manual checks.
- [x] Prepare isolated repository-local commit boundaries after local verification.
- [ ] Publish and update the umbrella pin only after user approval.

## Implementation decisions

- Canonical module declarations and bundle 2.0.0 are copied without local bundle changes. CMake includes
  `tooling/dependencies.cmake`; normal installed external discovery remains portable. There are no internal
  HoloNight providers and no Config consumer changes. Public targets, C++23 and component switches are retained.
- Every production Qt discovery and installed dependency export requires >=6.11. Tests require installed
  GTest >=1.14 CONFIG and both `GTest::gtest` and `GTest::gmock`. No private Qt components are introduced.
- Normal workflow commands validate before touching configured trees or preparing providers. Missing prerequisites
  preserve an existing tree; changed compatibility state archives the tree as `.previous-N`.
- Images are acquired explicitly with `--acquire-images`; acceptance containers use `--pull=never --network=none`.
  Rootless UID/GID mapping, read-only snapshots and private Storage D-Bus sessions are retained.
- Full Debug/Release, Audio-only, Storage-only, Compositor with/without Wayland, Core-only, full static checks and
  separate licensing lanes are provided. Core-only uses direct CMake: canonical workflow editor refresh requires
  compile commands, which an empty profile cannot provide. It does not invoke CTest.
- Removed unused QML preset variables to avoid irrelevant CMake diagnostics. Single-component installed consumers
  also reject absent components using their exact installed package directory, without host-provider fallback.
- Static snapshots initialize a disposable Git index because canonical checks enumerate tracked sources. Full tidy
  includes installed-consumer compilation databases; their configure commands export them and the static lane
  runs the install-consumer tests before analysis. Successful test logs are retained in lane output.
- Package identities use read-only `pacman --config /dev/null -Q`: the rootless account overlay omits Arch's
  `alpm` user. Qt Wayland is supplied by the qualified Qt package set; there is no separate installed `qt6-wayland`
  package. These reporting corrections do not acquire packages or alter the image.

## Verification — 2026-10-12

Canonical baseline: `git ls-remote origin refs/heads/main` returned the exact implementation baseline;
`git status --short --branch` showed clean `main...origin/main`. SSH inspection required sandbox escalation.

Explicit image preparation:

```sh
docker pull ghcr.io/lebedenko/holonight-deps@sha256:37ca051c39d1688d6413c47d94b8c0f221152dac6981ec67d155f2c43ea17b40
docker pull docker.io/fsfe/reuse@sha256:85462a75c0f8efda09ddd190b92816b70e7662577c8427429e11e1b9f25a992e
docker image inspect --format '{{.Id}} {{json .RepoDigests}}' <both references>
```

Full image ID: `sha256:51bb1aed43c0ad4e9336ed49962862ec78293f4380fd05e73b954e88c6c479c7`,
matching EDM-024. REUSE image ID: `sha256:2575b4ad3a736a14323c7705b708e0027109930a1511cce4e65cd09b9dfc1cc4`.

Focused verification before clean acceptance:

- `python3 scripts/ci/test_launcher.py`: 4 tests passed (snapshot isolation, rootless Podman account mapping,
  offline run flags, missing runtime and propagation of all lane failures).
- `python3 scripts/ci/test_dependencies.py` in the qualified offline image: 5 tests passed, covering missing/old
  Qt and GTest, missing GMock, disabled component dependency isolation, normal configure/build/test prerequisite
  failures without preparation, prefixes with spaces and preservation/archival of stale trees.
- Umbrella `python3 tooling/test_external.py`: 21 canonical regression tests passed.
- `cmp` of local module JSON/CMake against canonical declarations and
  `python3 tooling/sync.py --check --module holonight-system-services`: passed. Reviewed active CMake discovery,
  exported dependencies and all feature predicates against declarations (Qt Core/Network/Gui/WaylandClient/DBus,
  libpulse, wayland-client, GTest/GMock and Qt Test).
- `git diff --check`: passed before acceptance.

Initial clean attempts stopped during identity reporting (`alpm` account, then absent separate `qt6-wayland`
package); corrected together before compilation. A sandbox-only launcher attempt could not access Docker.
Those attempts are not acceptance evidence. REUSE passed in both initial acquired-image runs.

Debian qualification and final manual ecosystem integration remain deferred to the umbrella; EDM-022 remains
pending. No publication, CI result for a new revision, or umbrella pin update is claimed.

## Approved static cleanup

The required qualified-image static lane exposed existing formatting and lint drift. The user explicitly approved
separate cleanup commits while preserving public APIs. A qualified clang-format 22.1.8 pass changes 30 existing
files; whitespace-stripped contents match the original files exactly. Host clang-format 23.1.1 was used only
for read-only drift inspection, never to generate accepted changes.

The separate lint cleanup adds braces, removes avoidable copies, clarifies unused fixture parameters and makes
already-implied QObject non-copyability explicit. Existing public names, model role conversions and enum ABI
are retained with targeted annotations. Generated upstream protocol headers are system includes rather than
being rewritten. QObject-parented asynchronous watchers retain ownership and completion-time deletion; analyzer
exceptions identify that ownership explicitly. Transaction/topology decoding stays together with documented
complexity annotations. Qt signals, public function signatures, component switches and target names remain intact.

Development audit uses a disposable standalone snapshot with the same qualified image, compiler, Qt, feature
set and bundle. Artifacts are reused only at that compatible toolchain/profile; final clean acceptance follows
the batched corrections. Audit logs are under `build/ci/tidy-audit.log` and `tidy-corrections.log`; initial diagnostics
and mechanical fix review copies are outside the checkout. No acceptance success is inferred from an audit.

## Final clean acceptance

Command: `python3 scripts/ci/run.py` after the batched cleanup. Source snapshots are read-only and every lane
uses a new disposable writable tree; containers have no network and never pull. Complete logs and the machine
report are in `build/ci/20261012T174904Z-ddcikras/`. These supersede the pre-cleanup build evidence.

| Lane | Audio | Storage | Compositor | Wayland | Tests/profile | Result |
| --- | --- | --- | --- | --- | --- | --- |
| build-all | ON | ON | ON | ON | Debug: 84 Audio + 21 Storage + 90 Compositor tests, 3 installed consumers; Release: tests OFF | PASS |
| build-audio | ON | OFF | OFF | OFF | Debug: 84 backend tests + installed Audio consumer | PASS |
| build-storage | OFF | ON | OFF | OFF | Debug: 21 backend tests in private D-Bus session + installed Storage consumer | PASS |
| build-compositor | OFF | OFF | ON | ON | Debug: 90 backend tests + installed Compositor consumer | PASS |
| build-compositor-no-wayland | OFF | OFF | ON | OFF | Debug: 82 backend tests + installed Compositor consumer | PASS |
| build-core | OFF | OFF | OFF | OFF | Configure/build, tests OFF; no CTest | PASS |
| static-checks | ON | ON | ON | ON | Full format check, installed consumer databases, full tidy | PASS |
| licensing | — | — | — | — | Separate REUSE image | PASS |

Observed identities: Python 3.14.7, Git 2.55.0, CMake 4.4.3 (`4.4.3-2`), Ninja 1.13.2 (`1.13.2-3`),
GCC 16.2.1 (`16.2.1+r23+gd564253eb6c8-1`), Qt 6.11.2 (`qt6-base 6.11.2-3`),
GTest/GMock 1.18.0 (`gtest 1.18.0-1`), Clang/clang-format/clang-tidy 22.1.8 (`clang 22.1.8-1`),
libpulse 17.0-98-gb096 (`17.0+r98+gb096704c0-1`), Wayland 1.26.0 (`1.26.0-1`), pkgconf 3.0.7.
The launcher runs rootless as the current UID/GID (1000:1000), with synthetic passwd/group records.

Lane commands are versioned in `scripts/ci/lane.sh`: normal `tooling/workflow.py configure`, `build` and `test`
with the test preset; full Release `configure`/`build` with the release preset; Core direct CMake with all four
component switches and tests OFF. Static checks use `format-check`, `ctest --preset test -R install_consumer`
and `tidy --scope all`. Consumer CMake scripts configure/build/install the provider, then configure/build/run
the installed consumer and verify absent component requests fail.

The build-all lane reruns launcher (4) and dependency (5) regressions before compilation; all pass. The
disabled-component regression also leaves Wayland ON while Compositor is OFF, confirming the parent feature
guards Gui/Wayland discovery. Prefix regression verifies the selected Qt config path under the prefix with spaces.

Complete final build logs reviewed: full 1124 lines, Audio 388, Storage 245, Compositor 489, Compositor without
Wayland 395, Core 42. No compiler or CMake warnings/errors were present. Qt reports absent optional Vulkan
headers; this informational probe does not affect required OpenGL/Wayland discovery. Earlier targeted consumer
log review passed all four variants without compiler warnings (`build/ci/consumer-log-review.log`).

The initial static attempts exposed missing snapshot Git metadata, existing formatting drift, generated-header
classification and consumer compilation coverage. All corresponding corrections are included; required checks
remain enabled. Canonical full lint audit covered 37 owned translation units; after the remaining StorageController
corrections its focused strict lint check passed. Suppressed system-header diagnostics are not owned-code warnings.

Final launcher exit status: 0; every one of the eight `results.json` lane entries has exit code 0. Full strict tidy
completed all 37 owned translation units, including each installed consumer. The complete static and licensing
logs were reviewed; there were no owned-code diagnostics or licensing failures. Compiler/system-header counts
printed by clang-tidy were suppressed by its normal system-header policy and documented targeted annotations.

Final review: canonical declaration copies match, bundle drift check and both staged/working `git diff --check`
pass. Formatting-only changes retain identical non-whitespace content in all 30 files. Commit boundaries are
formatting, API-preserving lint cleanup, then EDM-005 tooling/discovery/CI/documentation adoption. No generated
dependency lockfiles, credentials or other repositories are changed. CI Python bytecode is ignored, and existing
untracked umbrella artifacts are left untouched. There are no module-owned Dockerfiles to change: the published
EDM-024 full image and the separate existing licensing image are used directly.

Publication handoff: prepare the three local commits on `main` for one authorized push to canonical
`origin/main`. After approval, verify remote availability, query latest CI once without waiting and create the
local umbrella checkpoint/pin update. Final ecosystem integration remains pending EDM-022.
