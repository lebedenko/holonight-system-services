# Local CI rehearsal

Baseline: 6938984c7fd4ffd165668beca19ea403abf954c4. Umbrella package CI-007.

Add missing build/test CI with independent Audio-only, Storage-only and combined
Debug/Ninja lanes in the immutable Qt6/compiler image, compile commands enabled
and BUILD_TESTS=ON. Run all available provider and installed-consumer CTest checks
with output-on-failure and no-tests=error. Keep components independently linkable;
strengthen the Audio install test to build only Audio and reject Storage/Qt DBus
imports. Run existing installed consumers after linking. Storage backend tests use
their own D-Bus session, without host services. Preserve the existing licensing
job, job result and triggers using the matching immutable REUSE 6.2.0 image.
Do not change service behavior, Qt registrations, presentation policy or dev tasks.

Snapshot tracked edits and non-ignored new inputs; omit deleted/ignored files and
preserve modes/symlinks. Report untracked inputs to add before pushing. Containers
mount input read-only and use disposable writable source/build trees. Save complete
logs, revision/dirty status, image identities, tool versions and results in build/ci.
Docker is preferred; absent Docker falls back to Podman with rootless UID/GID mapping.
Every failed/unavailable required check returns nonzero. No pushes or pin updates.
The launcher creates disposable passwd/group entries for its UID/GID and mounts
them read-only. D-Bus needs these entries to start a private test bus; host account
files and services are never mounted. The rootless Podman regression verifies the
account mapping and read-only mounts. Real Podman execution is unverified because
Podman is not installed.

2026-10-03 acceptance: `task ci` passes all four lanes. Combined component
CTest registers four passing checks; each single-component lane registers two,
covering provider and installed consumers. Full logs and metadata are in
`build/ci/20261003T103345Z-sx4f0oi2/`. Complete logs were reviewed without compiler
warnings. The earlier 72-file source/development-build isolation check passed
even when the missing-container-account failure made CI fail; the account fix
changes only disposable container mounts. Four launcher regression tests pass,
including account contents, snapshot modes/symlinks and missing/failed checks.
`reuse --no-multiprocessing lint` and `git diff --check` pass after documentation
closure. No pushes or submodule-pin updates.
