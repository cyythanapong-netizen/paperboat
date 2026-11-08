#!/usr/bin/env bash
# Linux release build: scripts/build_release.sh [out-dir]   (default dist/)
# A Release build with libstdc++ linked statically (the binary needs only glibc),
# the unit tests, then cpack -> .deb, .rpm (when rpmbuild is installed) and .tar.gz.
# CI runs it on ubuntu-22.04; locally: podman run --rm -v <export>:/src:z debian:bookworm /src/scripts/build_release.sh
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
out="$(realpath -m "${1:-$src/dist}")"  # absolute: cpack runs from the build directory
build="$src/engine/build"   # where bin/paperboat and the CLI tests look for it

cmake -S "$src/engine" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPAPERBOAT_STATIC_RUNTIME=ON \
  -DPAPERBOAT_BUILD_TESTS="${TESTS:-ON}"
cmake --build "$build"
if [ "${TESTS:-ON}" = ON ]; then (cd "$src" && "$build/tests/paperboat_tests"); fi
mkdir -p "$out"
generators="DEB;TGZ"
command -v rpmbuild >/dev/null && generators="DEB;RPM;TGZ"
(cd "$build" && cpack -G "$generators" -B "$out")
ls -l "$out"
