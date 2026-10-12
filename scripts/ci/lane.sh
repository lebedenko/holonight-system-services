#!/bin/sh
set -eu
lane=$1
mkdir /work/source
cp -a /input/. /work/source/
cd /work/source
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC QT_QPA_PLATFORM=offscreen
mkdir -p "$HOME"
if [ "$lane" = licensing ]; then
  reuse --version
  reuse lint
  exit
fi
audio=OFF; storage=OFF; compositor=OFF; wayland=OFF
case "$lane" in
  build-all|static-checks) audio=ON; storage=ON; compositor=ON; wayland=ON ;;
  build-audio) audio=ON ;;
  build-storage) storage=ON ;;
  build-compositor) compositor=ON; wayland=ON ;;
  build-compositor-no-wayland) compositor=ON ;;
  build-core) ;;
  *) echo "Unknown lane: $lane" >&2; exit 2 ;;
esac
python3 --version
git --version
cmake --version
ninja --version
c++ --version
pkg-config --modversion Qt6Core
pacman --config /dev/null -Q qt6-base gtest gcc clang cmake ninja libpulse wayland
export HOLONIGHT_BUILD_FEATURES="BUILD_AUDIO=$audio BUILD_STORAGE=$storage BUILD_COMPOSITOR=$compositor BUILD_COMPOSITOR_WAYLAND=$wayland"
if [ "$lane" = build-all ]; then
  python3 scripts/ci/test_launcher.py
  python3 scripts/ci/test_dependencies.py
fi
if [ "$lane" = build-core ]; then
  cmake -S . -B build/core -G Ninja -DBUILD_TESTS=OFF -DBUILD_AUDIO=OFF \
    -DBUILD_STORAGE=OFF -DBUILD_COMPOSITOR=OFF -DBUILD_COMPOSITOR_WAYLAND=OFF
  cmake --build build/core
  exit
fi
if [ "$lane" = static-checks ]; then
  # Canonical checks enumerate tracked files in this disposable snapshot.
  git init -q
  git add .
fi
python3 tooling/workflow.py configure --preset test
python3 tooling/workflow.py build --preset test
if [ "$lane" = static-checks ]; then
  clang-format --version
  clang-tidy --version
  python3 tooling/workflow.py format-check
  # Install consumers own translation units required by full tidy coverage.
  ctest --preset test -R install_consumer
  cat build/test/Testing/Temporary/LastTest.log
  python3 tooling/workflow.py tidy --scope all
else
  python3 tooling/workflow.py test --preset test
  cat build/test/Testing/Temporary/LastTest.log
fi
if [ "$lane" = build-all ]; then
  python3 tooling/workflow.py configure --preset release
  python3 tooling/workflow.py build --preset release
fi
