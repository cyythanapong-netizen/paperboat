#!/usr/bin/env python3
"""Build the npm packages for a release: one per platform (the native binary) and the launcher.

    cmake --install engine/build --prefix dist/root
    python3 scripts/pack_npm.py --version 0.1.0 --out dist/npm --target linux-x64 \\
        --prefix dist/root                             # @paperboatlabs/cli-linux-x64 (binary + SDK; or darwin-arm64)
    python3 scripts/pack_npm.py --version 0.1.0 --out dist/npm --main     # @paperboatlabs/cli

Each runs `npm pack`, leaving a .tgz in --out. Publish the platform packages first, the launcher last
(it lists them as optionalDependencies). Needs npm.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCOPE = "@paperboatlabs"  # one place to rename the scope
LICENSE = "Apache-2.0"  # SPDX id of the LICENSE file at the repository root
REPO = "git+https://github.com/paperboatlab/paperboat.git"

# target -> (npm os, npm cpu, libc, binary name)
TARGETS = {
    "linux-x64": ("linux", "x64", "glibc", "paperboat"),
    "darwin-arm64": ("darwin", "arm64", None, "paperboat"),
    "win32-x64": ("win32", "x64", None, "paperboat.exe"),
}


def pack(stage: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([shutil.which("npm") or "npm", "pack", "--silent", "--pack-destination", str(out.resolve())], cwd=stage, check=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--target", choices=sorted(TARGETS))
    ap.add_argument("--prefix", type=Path, help="a `cmake --install` tree for --target: bin/, share/ and the SDK")
    ap.add_argument("--main", action="store_true", help="the launcher package")
    args = ap.parse_args()
    if bool(args.main) == bool(args.target):
        sys.exit("give --main or --target")

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "pkg"
        if args.main:
            shutil.copytree(ROOT / "npm" / "cli", stage)
            shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
            meta = json.loads((stage / "package.json").read_text())
            meta["optionalDependencies"] = {f"{SCOPE}/cli-{t}": args.version for t in sorted(TARGETS)}
        else:
            os_name, cpu, libc, exe = TARGETS[args.target]
            if not args.prefix or not (args.prefix / "bin" / exe).is_file():
                sys.exit(f"--prefix must be an install tree with bin/{exe}")
            # bin/paperboat finds the registries at ../share/paperboat and the SDK at ../lib*/cmake/Paperboat.
            shutil.copytree(args.prefix, stage)
            (stage / "bin" / exe).chmod(0o755)
            (stage / "README.md").write_text(f"Native paperboat binary and C++ SDK for {args.target}. Install `{SCOPE}/cli`, not this.\n")
            shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
            meta = {
                "name": f"{SCOPE}/cli-{args.target}",
                "description": f"Paperboat command line, native binary for {args.target}",
                "repository": {"type": "git", "url": REPO},
                "os": [os_name],
                "cpu": [cpu],
                "files": sorted(p.name for p in stage.iterdir()),
            }
            if libc:
                meta["libc"] = [libc]
        meta["version"] = args.version
        meta["license"] = LICENSE
        (stage / "package.json").write_text(json.dumps(meta, indent=2) + "\n")
        pack(stage, args.out)
    print(f"packed {meta['name']}@{args.version} into {args.out}")


if __name__ == "__main__":
    main()
