#!/usr/bin/env python3
"""Build and run a Paperboat backend, installing any required toolkits first.

Usage:
    python3 scripts/run_backend.py examples/backends/parts-cpp
    python3 scripts/run_backend.py examples/backends/parts-cpp --port 8770 --no-build

A backend may declare extra toolkits in `toolkit_requirements.toml`:
    [[toolkits]]
    name = "opencv"
    repo = "https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv"

Missing toolkits are fetched via scripts/install_toolkit.py before building.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSTALL = ROOT / "scripts" / "install_toolkit.py"


def has_toolkit(name):
    return (ROOT / "engine" / "toolkit" / name).exists()


def read_requirements(backend_dir):
    p = Path(backend_dir)
    for cand in ("toolkit_requirements.toml", "toolkits.toml"):
        f = p / cand
        if f.exists():
            import tomllib
            with f.open("rb") as fh:
                data = tomllib.load(fh)
            return data.get("toolkits", [])
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("backend", help="path to the backend folder")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--clean", action="store_true")
    args = ap.parse_args()

    backend = Path(args.backend).resolve()
    if not backend.exists():
        print(f"backend not found: {backend}", file=sys.stderr)
        sys.exit(1)

    # 1. Install required toolkits that are missing.
    for t in read_requirements(backend):
        name = t["name"]
        if not has_toolkit(name):
            print(f"toolkit {name} required by {backend.name}: installing...")
            subprocess.run([sys.executable, str(INSTALL), name], check=True)
            # if repo override present, the install script used the manifest; ensure repo matches
            if "repo" in t and t["repo"]:
                # re-clone from the required repo if different
                pass

    # 2. Build (C++ backend) or ensure python deps.
    cmake_lists = backend / "CMakeLists.txt"
    server_py = backend / "server.py"
    if cmake_lists.exists() and not args.no_build:
        build_dir = backend / "build"
        if args.clean and build_dir.exists():
            import shutil
            shutil.rmtree(build_dir)
        print(f"building {backend.name}...")
        subprocess.run(["cmake", "-S", str(backend), "-B", str(build_dir)], check=True)
        subprocess.run(["cmake", "--build", str(build_dir)], check=True)
        # find binary
        bins = list(build_dir.glob("paperboat-*")) + list(build_dir.glob("*paperboat*"))
        if not bins:
            print("no binary found after build", file=sys.stderr)
            sys.exit(1)
        binary = bins[0]
    elif server_py.exists():
        binary = None
    else:
        print("nothing to run (no CMakeLists.txt or server.py)", file=sys.stderr)
        sys.exit(1)

    # 3. Run.
    env = dict(os.environ)
    cmd = []
    if binary is not None:
        cmd = [str(binary)]
        if args.port is not None:
            cmd += ["--port", str(args.port)]
    else:
        cmd = [sys.executable, str(server_py)]
        if args.port is not None:
            env["PORT"] = str(args.port)
    print(f"running: {' '.join(cmd)}")
    try:
        subprocess.run(cmd, env=env)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
