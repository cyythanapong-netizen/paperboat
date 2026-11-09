#!/usr/bin/env bash
# Linux release build: scripts/build_release.sh [out-dir]   (default dist/)
# A Release build with libstdc++ linked statically (the binary needs only glibc),
# the unit tests, then cpack -> .deb, .rpm (when rpmbuild is installed) and .tar.gz.
# PYTHONS="python3.11 python3.12" also builds the Python bindings with each (scripts/build_python.sh):
# the packages ship them in lib/paperboat/python (pb#223), and their wheels go to the out-dir.
# CI runs it on ubuntu-22.04; locally: podman run --rm -v <export>:/src:z debian:bookworm /src/scripts/build_release.sh
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
out="$(realpath -m "${1:-$src/dist}")"  # absolute: cpack runs from the build directory
build="$src/engine/build"   # where bin/paperboat and the CLI tests look for it

# The web app and the console go into the packages (share/paperboat/{web,cli}); they need npm and
# the sibling paperboat-shared checkout. WEB=OFF packages without them.
if [ "${WEB:-ON}" = ON ]; then (cd "$src" && npm ci --no-audit --no-fund && npm run build:release); fi

mkdir -p "$out"
python_modules=""
if [ -n "${PYTHONS:-}" ]; then
  # shellcheck disable=SC2086  # a list of interpreters
  "$src/scripts/build_python.sh" "$build/python" $PYTHONS
  cp "$build"/python/wheels/*.whl "$out/"
  python_modules="$build/python/modules"
fi

cmake -S "$src/engine" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPAPERBOAT_STATIC_RUNTIME=ON \
  -DPAPERBOAT_BUILD_TESTS="${TESTS:-ON}" \
  -DPAPERBOAT_PYTHON_MODULES_DIR="$python_modules"
cmake --build "$build"
if [ "${TESTS:-ON}" = ON ]; then (cd "$src" && "$build/tests/paperboat_tests"); fi
generators="DEB;TGZ"
command -v rpmbuild >/dev/null && generators="DEB;RPM;TGZ"
(cd "$build" && cpack -G "$generators" -B "$out")
ls -l "$out"
