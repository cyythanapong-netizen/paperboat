"""Golden results for every example backend: run each one's starter headless (`--run`) and
compare what it reports with the expected.json next to it.

The examples exist to prove the framework, so a framework change that alters any
example's result fails here. Every message is also checked against the protocol schema.

    python3 protocol/golden.py                 # every example with an expected.json
    python3 protocol/golden.py parts-cpp       # some of them
    python3 protocol/golden.py --examples DIR  # default: $PAPERBOAT_EXAMPLES_DIR, else ../paperboat-examples

expected.json: {"run": "build/paperboat-parts" | "server.py", "status": "done",
                "signals": {"node": value | [value, tolerance]},   # the node's last number
                "logs": ["text a log line contains", …]}
C++ examples must be built first (npm run backend:<name> builds them; or cmake).
"""

import json
import os
import subprocess
import os
import sys
from pathlib import Path

import jsonschema

ROOT = Path(__file__).resolve().parent.parent
# The paperboat-examples repo: --examples DIR, else $PAPERBOAT_EXAMPLES_DIR, else a checkout next to this one.
EXAMPLES = Path(os.environ.get("PAPERBOAT_EXAMPLES_DIR") or ROOT.parent / "paperboat-examples")
SCHEMA = json.loads((ROOT / "protocol" / "schema" / "paperboat-0.schema.json").read_text())
TO_UI = jsonschema.Draft202012Validator(SCHEMA)


def run(example: Path, expected: dict) -> list[str]:
    target = expected.get("run")
    if not target and (example / "backend.toml").exists():
        try:
            try:
                import tomllib
            except ImportError:
                import tomli as tomllib
            data = tomllib.loads((example / "backend.toml").read_text(encoding="utf-8"))
            target = data.get("backend", {}).get("run")
        except Exception:
            pass
    if not target:
        return ["no run target specified in expected.json or backend.toml"]
    if target.endswith(".py"):
        cmd = [sys.executable, target, "--run"]
        if "duration" in expected:
            cmd += ["--duration", str(expected["duration"])]
        # A checkout's python/ before what PYTHONPATH has: `paperboat check` from a package sets it
        # to the package's lib/paperboat/python, and there is no python/ next to protocol/ (pb#223).
        env = dict(os.environ)
        if (ROOT / "python" / "paperboat").is_dir():
            env["PYTHONPATH"] = os.pathsep.join(filter(None, [str(ROOT / "python"), os.environ.get("PYTHONPATH")]))
    elif target.endswith(".jl"):
        # A Julia backend is served by Paperboat.jl; run it in its own depot and project.
        julia = os.environ.get("PAPERBOAT_JULIA", "julia")
        env = dict(os.environ)
        env.setdefault("JULIA_DEPOT_PATH", str(example / ".julia"))
        env.setdefault("JULIA_LOAD_PATH", "@:@stdlib")
        env.setdefault("JULIA_NUM_THREADS", "1")
        if "PAPERBOAT_HOST_LIB" not in env:
            for name in ("libpaperboat_host.so", "libpaperboat_host.dylib", "paperboat_host.dll"):
                host = ROOT / "engine" / "build" / "host" / name
                if host.is_file():
                    env["PAPERBOAT_HOST_LIB"] = str(host)
                    break
        cmd = [julia, "--startup-file=no", "--project=" + str(example), str(example / target), "--run"]
        if "duration" in expected:
            cmd += ["--duration", str(expected["duration"])]
    else:
        if os.name == "nt" and not Path(target).suffix:  # a C++ backend's run names it without .exe
            target += ".exe"
        if not (example / target).exists():
            return [f"not built: {target}"]
        cmd, env = [str(example / target), "--run"], os.environ
        if "duration" in expected:
            cmd += ["--duration", str(expected["duration"])]
    proc = subprocess.run(cmd, cwd=example, env=env, capture_output=True, text=True, timeout=300)
    msgs, problems = [], []
    for line in proc.stdout.splitlines():
        if not line.startswith("{"):
            continue
        msg = json.loads(line)
        msgs.append(msg)
        for err in TO_UI.iter_errors(msg):
            problems.append(f"{msg.get('type')} breaks the schema: {err.message[:120]}")
    if not msgs or msgs[-1].get("type") != "run.finished":
        return problems + [f"no run.finished (exit {proc.returncode}): {proc.stderr.strip()[-300:]}"]

    status = msgs[-1]["status"]
    if status != expected.get("status", "done"):
        errors = [m["text"] for m in msgs if m["type"] == "log" and m["level"] == "error"]
        problems.append(f"status {status}, expected {expected.get('status', 'done')}: {errors}")
    last = {}
    for m in msgs:
        if m["type"] == "signal" and isinstance(m["v"], (int, float)):
            last[m["node"]] = m["v"]
    for node, want in expected.get("signals", {}).items():
        value, tol = (want[0], want[1]) if isinstance(want, list) else (want, 1e-9)
        if node not in last:
            problems.append(f"no signal from {node}")
        elif abs(last[node] - value) > tol:
            problems.append(f"{node} = {last[node]:.6g}, expected {value:.6g} ± {tol:g}")
    logs = [m["text"] for m in msgs if m["type"] == "log"]
    for text in expected.get("logs", []):
        if not any(text in line for line in logs):
            problems.append(f"no log line contains {text!r} (last: {logs[-1:]})")
    return problems


def main() -> None:
    global EXAMPLES
    args = sys.argv[1:]
    if "--examples" in args:
        i = args.index("--examples")
        EXAMPLES = Path(args[i + 1]).resolve()
        args = args[:i] + args[i + 2:]
    names = args or sorted(p.parent.name for p in EXAMPLES.glob("*/expected.json"))
    if not names:
        sys.exit(f"no backends with an expected.json in {EXAMPLES} (pass --examples DIR)")
    failed = 0
    for name in names:
        p = Path(name)
        if p.is_dir() and (p / "expected.json").exists():
            example = p.resolve()
            display_name = example.name
        else:
            example = EXAMPLES / name
            display_name = name
        if not (example / "expected.json").exists():
            print(f"FAIL {display_name}\n      expected.json not found in {example}", flush=True)
            failed += 1
            continue
        expected = json.loads((example / "expected.json").read_text())
        problems = run(example, expected)
        print(f"{'ok  ' if not problems else 'FAIL'} {display_name}" + "".join(f"\n      {p}" for p in problems), flush=True)
        failed += bool(problems)
    print("PASS" if not failed else f"{failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
