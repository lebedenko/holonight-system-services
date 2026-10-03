#!/bin/sh
set -eu
lane=$1
mkdir /work/source
cp -a /input/. /work/source/
cd /work/source
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC
mkdir -p "$HOME"
if [ "$lane" = licensing ]; then
  reuse --version
  reuse lint
  exit
fi
case "$lane" in
  build-all) audio=ON; storage=ON ;;
  build-audio) audio=ON; storage=OFF ;;
  build-storage) audio=OFF; storage=ON ;;
  *) echo "Unknown lane: $lane" >&2; exit 2 ;;
esac
python3 --version
git --version
cmake --version
ninja --version
c++ --version
pkg-config --modversion Qt6Core Qt6Test
if [ "$audio" = ON ]; then pkg-config --modversion libpulse; fi
if [ "$storage" = ON ]; then
  pkg-config --modversion Qt6DBus
  dbus-run-session --version
fi
python3 scripts/ci/test_launcher.py
cmake -S . -B build/verification -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTS=ON \
  -DBUILD_AUDIO="$audio" -DBUILD_STORAGE="$storage"
cmake --build build/verification --parallel 2
ctest --test-dir build/verification --output-on-failure --no-tests=error
