# Repository Guidelines

Public headers live under `include/holonight_system/<domain>/`; private backends live under `src/<domain>/`; tests
live under `tests/`. Keep components independently linkable and do not add QML registration, translations, or
application presentation policy to shared classes.

Use C++23 and the checked-in CMake package. Configure with `cmake -S . -B build -G Ninja -DBUILD_TESTS=ON`, build
with `cmake --build build`, and run `ctest --test-dir build --output-on-failure`. Each component must retain an
install-tree consumer test and deterministic backend-interface coverage.
