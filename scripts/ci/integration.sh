#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task build:release
DESTDIR="$PWD/build/stage" cmake --install build/release
cmake -S . -B build/audio -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_AUDIO=ON -DBUILD_STORAGE=OFF -DBUILD_COMPOSITOR=OFF -DBUILD_COMPOSITOR_WAYLAND=OFF
cmake --build build/audio
ctest --test-dir build/audio --output-on-failure --no-tests=error
cmake -S . -B build/storage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_AUDIO=OFF -DBUILD_STORAGE=ON -DBUILD_COMPOSITOR=OFF -DBUILD_COMPOSITOR_WAYLAND=OFF
cmake --build build/storage
ctest --test-dir build/storage --output-on-failure --no-tests=error
cmake -S . -B build/compositor -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_AUDIO=OFF -DBUILD_STORAGE=OFF -DBUILD_COMPOSITOR=ON -DBUILD_COMPOSITOR_WAYLAND=ON
cmake --build build/compositor
ctest --test-dir build/compositor --output-on-failure --no-tests=error
cmake -S . -B build/compositor-no-wayland -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_AUDIO=OFF -DBUILD_STORAGE=OFF -DBUILD_COMPOSITOR=ON -DBUILD_COMPOSITOR_WAYLAND=OFF
cmake --build build/compositor-no-wayland
ctest --test-dir build/compositor-no-wayland --output-on-failure --no-tests=error
