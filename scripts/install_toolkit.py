#!/usr/bin/env python3
"""Install a Paperboat toolkit from its GitHub repository.

Usage:
    python3 scripts/install_toolkit.py opencv
    python3 scripts/install_toolkit.py --list
    python3 scripts/install_toolkit.py --all

Reads `toolkits.toml` at the repo root for the list of available toolkits, clones the
named one into `engine/toolkit/<name>` (overlaid so the engine discovers it), and builds
the engine. Designed to be called by `paperboat toolkit add <name>` once the CLI ships.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLKITS_TOML = ROOT / "toolkits.toml"
TOOLKIT_DIR = ROOT / "engine" / "toolkit"


def load_manifest():
    if not TOOLKITS_TOML.exists():
        print(f"missing {TOOLKITS_TOML}", file=sys.stderr)
        sys.exit(1)
    with TOOLKITS_TOML.open("rb") as f:
        data = tomllib.load(f)
    return {t["name"]: t for t in data.get("toolkits", [])}


def list_toolkits(manifest):
    for name, info in manifest.items():
        status = "installed" if (TOOLKIT_DIR / name).exists() else "not installed"
        print(f"{name:12} {status:14} {info.get('description', '')}")


def install(name, manifest):
    if name not in manifest:
        print(f"unknown toolkit: {name}", file=sys.stderr)
        sys.exit(1)
    info = manifest[name]
    repo = info["repo"]
    dest = TOOLKIT_DIR / name
    if dest.exists():
        print(f"{name}: already present at {dest}; pulling latest")
        subprocess.run(["git", "-C", str(dest), "pull", "--ff-only"], check=True)
    else:
        print(f"{name}: cloning {repo}")
        subprocess.run(["git", "clone", "--depth", "1", repo, str(dest)], check=True)
    # The cloned repo stores the toolkit under engine/toolkit/<name>; if the clone placed
    # it at the repo root's engine/toolkit/<name>, flatten it.
    nested = dest / "engine" / "toolkit" / name
    if nested.exists() and nested != dest:
        for item in nested.iterdir():
            target = dest / item.name
            if target.exists():
                if target.is_dir():
                    shutil.rmtree(target)
                else:
                    target.unlink()
            shutil.move(str(item), str(target))
        shutil.rmtree(dest / "engine", ignore_errors=True)
    print(f"{name}: installed at {dest}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    manifest = load_manifest()
    if args.list:
        list_toolkits(manifest)
        return
    if args.all:
        for name in manifest:
            install(name, manifest)
        return
    if not args.name:
        ap.error("provide a toolkit name, or use --list / --all")
    install(args.name, manifest)


if __name__ == "__main__":
    main()
