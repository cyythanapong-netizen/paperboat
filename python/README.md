# paperboat (Python bindings)

Write a Paperboat backend's domain code in Python; the C++ engine runs it. The extension
`paperboat._engine` (nanobind, `src/engine.cpp`) loads the descriptor, checks and runs
graphs (Signal flow on a clock, Step flow, run-once), runs the toolkit's blocks and serves
the protocol. Python only provides the domain's own blocks and state:

```python
import paperboat

class Force(paperboat.Block):            # defined by its attributes, run by its methods
    type, label, kind = "F1", "F1 · Force on mass", "source"
    inputs = ["force"]

    def step(self, ctx, inputs):
        ctx.state.force += inputs.get("force", 0.0)
        return {}

class MassSpring(paperboat.DomainState):
    def step(self, ctx): ...             # after each clocked pass
    def entities(self): return [...]     # what the Station shows

paperboat.serve(paperboat.Backend(engine="mass-spring", descriptor="descriptor.toml",
                                  blocks=[Force], state=MassSpring, mode="clock", port=8765))
```

The classes become the domain's package (named after the descriptor's `domain`), so the
palette has them without the descriptor listing them. Also: `paperboat.Monitor`,
`paperboat.Step` (Step flow), `paperboat.StationObject` (with `block = Linked(...)`),
`Port(id, type)`, `Param(default, "key" | "file")`, and `ctx.log(text, level)` for the
Console. `python3 server.py --run` runs the starter once, headless. Reference: `docs/api`.

Never implement a framework feature here: add it to `engine/`, then bind it.

## Build

Needs a C++20 compiler, CMake, and the Python headers (`python3-devel`). nanobind is
fetched.

```sh
npm run build:python        # dev: builds paperboat/_engine*.so in place (PYTHONPATH=python)
pip install ./python        # or install it (scikit-build-core)
```

The packages (.tar.gz, .zip) need none of this: they ship the package in
`<prefix>/lib/paperboat/python`, which the CLI puts on `PYTHONPATH` for a Python backend. The
release builds it with `scripts/build_python.sh` (a wheel per interpreter): `_engine.abi3.so`
(`.pyd` on Windows), nanobind's stable ABI, for every CPython from 3.12 on, and a module for 3.11
(Debian 12's python3), which 3.11 picks first. `-DPAPERBOAT_PYTHON_STABLE_ABI=ON` builds the abi3
module with CMake. The minimum is Python 3.11 (`PAPERBOAT_PYTHON` picks the interpreter).
