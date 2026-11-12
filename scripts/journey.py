#!/usr/bin/env python3
"""End-to-end check of a built checkout, as a user drives it: the same steps on Linux, macOS and Windows.

    python3 scripts/journey.py --examples ../paperboat-examples [--toolkit-repo URL|DIR] [--keep]

1. launcher   bin/paperboat (bin\\paperboat.cmd on Windows) runs and prints its version
2. workbench  `paperboat start --daemon` serves the web app and starts the workbench, which passes the smoke test
3. example    an example backend passes `paperboat check`, then `paperboat start`s and passes the smoke test
4. toolkit    `paperboat toolkit install optimization` installs and builds an optional toolkit
5. toolkit example  one of its examples passes `paperboat check`

Needs the engine built (engine/build), the web app built (npm run build), the Python bindings for a
Python example (python/), and `pip install websockets jsonschema`. Exit status 0 when every step
passed. Without --keep, the toolkit is uninstalled again at the end.
"""
from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS = os.name == "nt"
EXAMPLE = "01-smart-thermostat-python"  # Python, no toolkit: the cheapest to build and run
TOOLKIT = "optimization"
TOOLKIT_EXAMPLE = "opt-01-beam-sizing"  # C++, the optimization toolkit only (opt-03 needs Ipopt)
TOOLKIT_REPO = "https://github.com/paperboatlab/paperboat-toolkit-optimization.git"

results: list[tuple[str, bool, float, str]] = []


def launcher() -> Path:
    return ROOT / "bin" / ("paperboat.cmd" if WINDOWS else "paperboat")


def cli() -> str:
    """The built binary itself, for the steps that start long-lived processes (no cmd.exe in between)."""
    tools = ROOT / "engine" / "build"
    names = ["paperboat.exe"] if WINDOWS else ["paperboat"]
    for sub in ("dev/tools", "tools", "tools/Release", "tools/RelWithDebInfo", "tools/Debug"):
        for name in names:
            if (tools / sub / name).is_file():
                return str(tools / sub / name)
    return str(launcher())


def run(args: list[str], timeout: float = 1800, **kw) -> subprocess.CompletedProcess:
    print("  $", " ".join(args), flush=True)
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL, **kw)


def tail(proc: subprocess.CompletedProcess, lines: int = 15) -> str:
    text = (proc.stdout or "") + (proc.stderr or "")
    return "\n".join(text.strip().splitlines()[-lines:])


def listening(port: int) -> bool:
    with socket.socket() as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def wait_port(port: int, up: bool = True, seconds: float = 120) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        if listening(port) == up:
            return True
        time.sleep(0.5)
    return False


def smoke(port: int) -> subprocess.CompletedProcess:
    return run([sys.executable, str(ROOT / "protocol" / "smoke_test.py"), "--port", str(port)], timeout=300)


def step(name: str):
    """Decorator: run the step, record pass/fail with its time and the reason it failed."""
    def wrap(fn):
        def go(*a, **kw) -> bool:
            print(f"\n== {name}", flush=True)
            start = time.time()
            try:
                why = fn(*a, **kw) or ""
            except Exception as error:  # a step that crashes is a failed step, and the rest still run
                why = f"{type(error).__name__}: {error}"
            ok = not why
            results.append((name, ok, time.time() - start, why))
            print(f"   {'PASS' if ok else 'FAIL'} {name}" + ("" if ok else f"\n{why}"), flush=True)
            return ok
        return go
    return wrap


@step("1 launcher: bin/paperboat --version")
def check_launcher() -> str:
    r = run([str(launcher()), "--version"], timeout=600)
    if r.returncode != 0 or "paperboat" not in r.stdout:
        return f"exit {r.returncode}:\n{tail(r)}"
    return ""


@step("2 workbench: `paperboat start` serves the app and starts the workbench")
def check_workbench() -> str:
    bin_ = cli()
    try:
        r = run([bin_, "start", "--daemon", "--port", "3047"], timeout=900)
        if r.returncode != 0:
            return f"start exit {r.returncode}:\n{tail(r)}"
        if not wait_port(3047):
            return "the web app never listened on 3047"
        page = urllib.request.urlopen("http://127.0.0.1:3047/", timeout=10).read().decode("utf-8", "replace")
        if 'id="root"' not in page:
            return "3047 answered without the web app's page"
        if not wait_port(8765):
            return "the workbench never listened on 8765"
        s = smoke(8765)
        return "" if s.returncode == 0 else f"workbench smoke test failed:\n{tail(s)}"
    finally:
        run([bin_, "stop"], timeout=60)
        run([bin_, "stop", "8765"], timeout=60)
        wait_port(3047, up=False, seconds=20)
        wait_port(8765, up=False, seconds=20)


def start_and_smoke(backend: Path, port: int) -> str:
    """`paperboat start <backend>` (foreground), the smoke test against it, then `paperboat stop <port>`."""
    bin_ = cli()
    print(f"  $ {bin_} start {backend} --port {port}  (in the background)", flush=True)
    proc = subprocess.Popen([bin_, "start", str(backend), "--port", str(port)], stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if not wait_port(port, seconds=900):
            proc.kill()
            out = proc.communicate(timeout=30)[0] or ""
            return f"never listened on {port}:\n" + "\n".join(out.strip().splitlines()[-15:])
        s = smoke(port)
        return "" if s.returncode == 0 else f"smoke test failed:\n{tail(s)}"
    finally:
        run([bin_, "stop", str(port)], timeout=60)
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()


@step(f"3 example: {EXAMPLE} checks, starts and passes the smoke test")
def check_example(examples: Path) -> str:
    backend = examples / EXAMPLE
    if not backend.is_dir():
        return f"no {backend} (pass --examples)"
    r = run([cli(), "check", str(backend)])
    if r.returncode != 0:
        return f"check exit {r.returncode}:\n{tail(r)}"
    return start_and_smoke(backend, 8781)


@step(f"4 toolkit: `paperboat toolkit install {TOOLKIT}`")
def install_toolkit(source: str) -> str:
    r = run([cli(), "toolkit", "install", TOOLKIT, "--repo", source], timeout=3600)
    if r.returncode != 0:
        return f"install exit {r.returncode}:\n{tail(r, 25)}"
    listed = run([cli(), "toolkit", "list"], timeout=60)
    row = next((line for line in listed.stdout.splitlines() if line.split() and line.split()[0] == TOOLKIT), "")
    if not row or "not installed" in row:
        return f"`toolkit list` does not show {TOOLKIT} installed:\n{listed.stdout}"
    return ""


@step(f"5 toolkit example: {TOOLKIT_EXAMPLE} passes `paperboat check`")
def check_toolkit_example() -> str:
    backend = ROOT / "engine" / "toolkit" / TOOLKIT / "examples" / TOOLKIT_EXAMPLE
    if not backend.is_dir():
        return f"the install brought no {backend}"
    r = run([cli(), "check", str(backend)], timeout=3600)
    return "" if r.returncode == 0 else f"check exit {r.returncode}:\n{tail(r, 25)}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--examples", type=Path, default=ROOT.parent / "paperboat-examples", help="a paperboat-examples checkout")
    parser.add_argument("--toolkit-repo", default=TOOLKIT_REPO, help="where to install the optimization toolkit from (URL or folder)")
    parser.add_argument("--keep", action="store_true", help="leave the toolkit installed")
    parser.add_argument("--skip", action="append", default=[], metavar="N", help="skip step N (repeatable)")
    args = parser.parse_args()
    os.chdir(Path.home())  # outside the checkout, as a user runs it; the CLI must find its root itself

    skip = set(args.skip)
    if "1" not in skip:
        check_launcher()
    if "2" not in skip:
        check_workbench()
    if "3" not in skip:
        check_example(args.examples.resolve())
    installed_here = False
    if "4" not in skip:
        installed_here = install_toolkit(args.toolkit_repo)
    if "5" not in skip:
        check_toolkit_example()
    if installed_here and not args.keep:
        print(f"\n== cleanup: uninstall {TOOLKIT}", flush=True)
        run([cli(), "toolkit", "uninstall", TOOLKIT], timeout=3600)

    print("\n" + "-" * 72)
    for name, ok, seconds, _ in results:
        print(f"{'PASS' if ok else 'FAIL'}  {seconds:6.0f}s  {name}")
    failed = [r for r in results if not r[1]]
    print("-" * 72 + f"\n{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
