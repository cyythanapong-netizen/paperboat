#!/usr/bin/env python3
"""Build the npm packages for a release: one per platform (the native binary) and the launcher.

    python3 scripts/pack_npm.py --version 0.1.0 --out dist/npm --target linux-x64 \\
        --binary engine/build/tools/paperboat          # @paperboat/cli-linux-x64
    python3 scripts/pack_npm.py --version 0.1.0 --out dist/npm --main     # @paperboat/cli

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
SCOPE = "@paperboat"  # one place to rename the scope
LICENSE = "UNLICENSED"  # no LICENSE file in the repository yet; set the SPDX id when one is chosen
REPO = "git+https://github.com/cyythanapong-netizen/paperboat.git"

# target -> (npm os, npm cpu, libc, binary name)
TARGETS = {
    "linux-x64": ("linux", "x64", "glibc", "paperboat"),
    "win32-x64": ("win32", "x64", None, "paperboat.exe"),
}


def pack(stage: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(["npm", "pack", "--silent", "--pack-destination", str(out.resolve())], cwd=stage, check=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--target", choices=sorted(TARGETS))
    ap.add_argument("--binary", type=Path, help="the paperboat binary for --target")
    ap.add_argument("--main", action="store_true", help="the launcher package")
    args = ap.parse_args()
    if bool(args.main) == bool(args.target):
        sys.exit("give --main or --target")

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "pkg"
        if args.main:
            shutil.copytree(ROOT / "npm" / "cli", stage)
            meta = json.loads((stage / "package.json").read_text())
            meta["optionalDependencies"] = {f"{SCOPE}/cli-{t}": args.version for t in sorted(TARGETS)}
        else:
            if not args.binary or not args.binary.is_file():
                sys.exit("--binary must be the paperboat executable for --target")
            os_name, cpu, libc, exe = TARGETS[args.target]
            (stage / "bin").mkdir(parents=True)
            shutil.copy2(args.binary, stage / "bin" / exe)
            (stage / "bin" / exe).chmod(0o755)
            share = stage / "share" / "paperboat"  # where the binary looks for the registries: ../share/paperboat
            share.mkdir(parents=True)
            for name in ("toolkits.toml", "apps.toml"):
                shutil.copy2(ROOT / name, share / name)
            (stage / "README.md").write_text(f"Native paperboat binary for {args.target}. Install `{SCOPE}/cli`, not this.\n")
            meta = {
                "name": f"{SCOPE}/cli-{args.target}",
                "description": f"Paperboat command line, native binary for {args.target}",
                "repository": {"type": "git", "url": REPO},
                "os": [os_name],
                "cpu": [cpu],
                "files": ["bin", "share", "README.md"],
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
