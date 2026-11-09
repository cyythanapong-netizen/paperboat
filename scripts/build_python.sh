#!/usr/bin/env bash
# The Python bindings for the release packages (pb#223): scripts/build_python.sh <out-dir> <python>...
# With each interpreter, a wheel of python/ (pip wheel, scikit-build-core) into <out-dir>/wheels:
# CPython 3.12 or later makes the abi3 wheel (nanobind's stable ABI: every CPython from 3.12 on),
# 3.11 a cp311 wheel. Their paperboat._engine modules go to <out-dir>/modules, which the packages
# ship (cmake -DPAPERBOAT_PYTHON_MODULES_DIR=<out-dir>/modules, see engine/tools/CMakeLists.txt).
# More CMake flags for the extension: CMAKE_ARGS, e.g. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: scripts/build_python.sh <out-dir> <python>...}"
shift
[ $# -gt 0 ] || { echo "usage: scripts/build_python.sh <out-dir> <python>..." >&2; exit 2; }
# A wheel takes everything in python/paperboat: an in-place build's module would go into each one.
if compgen -G "$src/python/paperboat/_engine.*" >/dev/null; then
  echo "error: remove python/paperboat/_engine.* (an in-place build, npm run build:python) first" >&2
  exit 1
fi
rm -rf "$out/wheels" "$out/modules"
mkdir -p "$out/wheels" "$out/modules"
out="$(cd "$out" && pwd)"

# Every core (the Visual Studio generator builds one file at a time otherwise).
CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
export CMAKE_BUILD_PARALLEL_LEVEL

for py in "$@"; do
  echo "== paperboat wheel with $("$py" -c 'import sys; print(sys.version.split()[0], sys.executable)')"
  "$py" -m pip wheel --no-deps --wheel-dir "$out/wheels" "$src/python"
done

# The module from each wheel: _engine.abi3.so / _engine.cpython-311-*.so, _engine.pyd / _engine.cp311-*.pyd.
"$1" - "$out" <<'EOF'
import sys, zipfile
from pathlib import Path
out = Path(sys.argv[1])
for wheel in sorted((out / "wheels").glob("paperboat-*.whl")):
    with zipfile.ZipFile(wheel) as z:
        for name in z.namelist():
            if name.startswith("paperboat/_engine."):
                (out / "modules" / Path(name).name).write_bytes(z.read(name))
                print(f"{wheel.name}: {name}")
EOF
ls -l "$out/wheels" "$out/modules"
