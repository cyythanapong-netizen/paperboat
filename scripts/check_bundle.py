#!/usr/bin/env python3
"""Optional toolkits never enter a release artifact (pb#297).

An optional toolkit is installed by the user into ``engine/toolkit/<name>/`` and may carry a
licence the engine does not (the mesh toolkit is GPL-3.0; the engine is Apache-2.0). An artifact
that carries one is a combined work under that licence, so this checker fails on every trace of
one:

    scripts/check_bundle.py --prefix <install tree>          # a `cmake --install` output
    scripts/check_bundle.py --archive <tar.gz|zip|whl|deb|rpm> ...
    scripts/check_bundle.py --list                           # the names, from toolkits.toml
    scripts/check_bundle.py --self-test                      # the checker's own tests

It reads the optional names from the root ``toolkits.toml`` and from any installed
``engine/toolkit/<name>/toolkit.toml``: a toolkit counts as optional whether or not the catalog
still mentions it. Exit 0 when clean, 1 when something would ship, 2 on a usage or tooling error.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib
import zipfile
from pathlib import Path, PurePosixPath

TAR = (".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".tar.zst")
ZIP = (".zip", ".whl", ".egg", ".jar")


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def optional_names(root: Path) -> set[str]:
    """The optional package names, from the catalog and from installed toolkit manifests."""
    names: set[str] = set()

    catalog = root / "toolkits.toml"
    if catalog.is_file():
        data = tomllib.loads(catalog.read_text(encoding="utf-8"))
        for entry in data.get("toolkits", []):
            if entry.get("optional") is True and entry.get("name"):
                names.add(str(entry["name"]))

    # engine/toolkit/<name>/toolkit.toml: the install point. A toolkit is optional even if the
    # catalog no longer names it, so a stale catalog cannot smuggle one into an artifact.
    for manifest in sorted((root / "engine" / "toolkit").glob("*/toolkit.toml")):
        try:
            data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            continue
        if data.get("optional") is True and data.get("name"):
            names.add(str(data["name"]))

    return names


def normalise(name: str) -> str:
    name = name.replace("\\", "/")
    while name.startswith("./"):
        name = name[2:]
    return name.lstrip("/")


def reasons(entry: str, names: set[str]) -> list[str]:
    """Why `entry` may not ship, if it may not."""
    parts = PurePosixPath(normalise(entry)).parts
    if not parts:
        return []
    found: list[str] = []

    # 1. share/paperboat/toolkit/** is where a toolkit's installed data would land.
    for i in range(len(parts) - 2):
        if parts[i : i + 3] == ("share", "paperboat", "toolkit"):
            found.append("share/paperboat/toolkit/")
            break

    # 2. <...>/toolkit/<name>/**: a source copy, its headers, or its installed data.
    for i in range(len(parts) - 1):
        if parts[i] == "toolkit" and parts[i + 1] in names:
            found.append(f"toolkit/{parts[i + 1]}")
            break

    # 3. A rebuilt target: libpaperboat_toolkit_<name>.a, paperboat_toolkit-<name>-config.cmake.
    base = parts[-1].lower()
    for name in names:
        token = name.lower()
        if f"toolkit_{token}" in base or f"toolkit-{token}" in base:
            found.append(f"the {name} library or CMake target")
            break

    # 4. A header beside the built-ins: include/paperboat/toolkit/<name>.hpp.
    if base in {f"{name.lower()}.hpp" for name in names} | {f"{name.lower()}.h" for name in names}:
        found.append(f"the {base.rsplit('.', 1)[0]} header")

    return found


def check(entries, names: set[str]) -> list[tuple[str, list[str]]]:
    offences = []
    for entry in entries:
        why = reasons(entry, names)
        if why:
            offences.append((entry, why))
    return offences


def prefix_entries(prefix: Path):
    for dirpath, dirnames, filenames in os.walk(prefix, followlinks=False):
        here = Path(dirpath)
        for name in dirnames:
            yield (here / name).relative_to(prefix).as_posix()
        for name in filenames:
            yield (here / name).relative_to(prefix).as_posix()


def tar_entries(path: Path):
    try:
        with tarfile.open(path, "r:*") as archive:
            for member in archive:
                yield member.name
        return
    except tarfile.ReadError:
        pass
    # zstd: the stdlib tar reader predates it (a .deb's data.tar.zst, a .tar.zst).
    result = subprocess.run(["tar", "-tf", str(path)], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"{path}: cannot read this archive ({result.stderr.strip() or 'unknown format'})")
    for line in result.stdout.splitlines():
        if line.strip():
            yield line


def zip_entries(path: Path):
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            yield name


def deb_entries(path: Path):
    if shutil.which("ar") is None:
        raise SystemExit(f"{path}: needs `ar` (binutils) to read a .deb")
    with tempfile.TemporaryDirectory() as tmp:
        # ar runs in the scratch folder, so hand it an absolute path (the CLI takes relative ones).
        result = subprocess.run(["ar", "x", str(path.resolve())], cwd=tmp, capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(f"{path}: ar failed ({result.stderr.strip()})")
        data = [Path(tmp) / name for name in os.listdir(tmp) if name.startswith("data.tar")]
        if not data:
            raise SystemExit(f"{path}: no data.tar member")
        yield from tar_entries(data[0])


def rpm_entries(path: Path):
    for tool in ("rpm2cpio", "cpio"):
        if shutil.which(tool) is None:
            raise SystemExit(f"{path}: needs {tool} to read a .rpm")
    command = f"rpm2cpio {shlex.quote(str(path))} | cpio -t --quiet 2>/dev/null"
    result = subprocess.run(["sh", "-c", command], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"{path}: rpm2cpio failed")
    for line in result.stdout.splitlines():
        if line.strip():
            yield line


def archive_entries(path: Path):
    lower = path.name.lower()
    if lower.endswith(TAR):
        yield from tar_entries(path)
    elif lower.endswith(ZIP):
        yield from zip_entries(path)
    elif lower.endswith(".deb"):
        yield from deb_entries(path)
    elif lower.endswith(".rpm"):
        yield from rpm_entries(path)
    else:
        raise SystemExit(f"{path}: not an archive this checker reads (tar.*, zip, whl, deb, rpm)")


def report(offences: list[tuple[str, list[str]]], source: str) -> int:
    if not offences:
        return 0
    for entry, why in offences:
        print(f"bundled optional toolkit: {entry} ({', '.join(why)})")
    print(f"\n{source}: {len(offences)} file(s) from an optional toolkit would ship.")
    print("Optional toolkits are never bundled (pb#297): the engine is Apache-2.0, a toolkit")
    print("carries its own licence, and a user installs one with `paperboat toolkit install <name>`.")
    return 1


def self_test() -> int:
    """The checker's own tests: a clean tree, every rule, and two archives."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        root = tmp_path / "root"
        (root / "engine" / "toolkit").mkdir(parents=True)
        (root / "toolkits.toml").write_text(
            '[[toolkits]]\nname = "zzmesh"\noptional = true\n'
            '[[toolkits]]\nname = "zzbuilt"\noptional = false\n',
            encoding="utf-8",
        )
        names = optional_names(root)
        assert names == {"zzmesh"}, f"optional_names: {names}"

        def tree(label: str, files: list[str]) -> Path:
            directory = tmp_path / label
            for name in files:
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("x", encoding="utf-8")
            directory.mkdir(parents=True, exist_ok=True)
            return directory

        clean = tree(
            "clean",
            [
                "bin/paperboat",
                "share/paperboat/toolkits.toml",
                "share/paperboat/protocol/smoke_test.py",
                "include/paperboat/toolkit/block.hpp",
                "lib/paperboat/python/paperboat/__init__.py",
                "lib/libpaperboat_toolkit_zzbuilt.so",  # built-in: allowed
            ],
        )
        assert not check(prefix_entries(clean), names), "a clean install tree was flagged"

        bad = {
            "share/paperboat/toolkit/zzmesh/toolkit.toml": "share/paperboat/toolkit/",
            "engine/toolkit/zzmesh/toolkit.toml": "toolkit/zzmesh",
            "include/paperboat/toolkit/zzmesh.hpp": "the zzmesh header",
            "lib/libpaperboat_toolkit_zzmesh.so": "the zzmesh library",
        }
        for entry, expected in bad.items():
            directory = tree("bad-" + entry.replace("/", "_"), [entry])
            offences = check(prefix_entries(directory), names)
            assert offences, f"{entry} was not flagged"
            assert any(expected in why for why in offences[0][1]), f"{entry}: {offences[0][1]}"

        # A directory with no files is still a bundled toolkit.
        empty = tree("empty", [])
        (empty / "share" / "paperboat" / "toolkit" / "zzmesh").mkdir(parents=True)
        assert check(prefix_entries(empty), names), "an empty toolkit folder was not flagged"

        # Archives: the same entry, as tar.gz and as zip.
        for label, member, opener in (
            ("bundle.tgz", "share/paperboat/toolkit/zzmesh/toolkit.toml", "tar"),
            ("bundle.zip", "share/paperboat/toolkit/zzmesh/toolkit.toml", "zip"),
        ):
            archive = tmp_path / label
            if opener == "tar":
                with tarfile.open(archive, "w:gz") as handle:
                    handle.add(empty / "share" / "paperboat" / "toolkit" / "zzmesh", arcname=member.rsplit("/", 1)[0])
            else:
                with zipfile.ZipFile(archive, "w") as handle:
                    handle.writestr(member, "x")
            assert check(archive_entries(archive), names), f"{label} was not flagged"

        print("self-test: ok")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=repo_root(), help="the checkout that holds toolkits.toml")
    parser.add_argument("--prefix", type=Path, help="an install tree (a `cmake --install` output)")
    parser.add_argument(
        "--archive", type=Path, action="extend", nargs="+", default=[], metavar="FILE",
        help="packages to read (several allowed)",
    )
    parser.add_argument("--list", action="store_true", help="print the optional names and exit")
    parser.add_argument("--self-test", action="store_true", help="test the checker itself")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    try:
        names = optional_names(args.root)
    except (OSError, tomllib.TOMLDecodeError) as error:
        print(f"{args.root / 'toolkits.toml'}: {error}", file=sys.stderr)
        return 2

    if args.list:
        for name in sorted(names):
            print(name)
        return 0

    if not args.prefix and not args.archive:
        parser.error("give --prefix, --archive, --list or --self-test")

    failed = 0
    if args.prefix:
        if not args.prefix.is_dir():
            print(f"{args.prefix}: not a directory", file=sys.stderr)
            return 2
        failed |= report(check(prefix_entries(args.prefix), names), str(args.prefix))

    for archive in args.archive:
        if not archive.is_file():
            print(f"{archive}: not a file", file=sys.stderr)
            return 2
        failed |= report(check(archive_entries(archive), names), str(archive))

    return failed


if __name__ == "__main__":
    sys.exit(main())
