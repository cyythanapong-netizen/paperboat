"""The Python bindings, end to end: a tiny backend written in Python runs its starter on
the C++ engine (`--run`), and what it reports is checked. Run: pytest python/tests
(with the extension built: npm run build:python, or pip install ./python)."""

import json
import os
import subprocess
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

BACKEND = '''
import paperboat
from paperboat import Linked, Port

class Double(paperboat.Block):
    type, label = "double", "Double"
    inputs, outputs = ["u"], ["y"]
    params = {"k": 2.0, "loud": False, "name": "twice"}
    def step(self, ctx, inputs):
        if self.params["loud"]:
            ctx.log(self.params["name"])
        return {"y": self.params["k"] * inputs.get("u", 0.0)}

class Square(paperboat.Generator):
    type, label = "square-of-t", "t squared"
    def value(self, t):
        return t * t

class Slide(paperboat.Timed):
    type, label = "slide", "Slide"
    params = {"seconds": 1.0}
    def begin(self, ctx, inputs):
        ctx.state.x = 0.0
    def advance(self, ctx, start, end):
        ctx.state.x += end - start

class World(paperboat.DomainState):
    def __init__(self):
        self.x = -1.0
    def summary(self):
        return f"x = {self.x:.3f}"

class Weight(paperboat.StationObject):
    type, label = "weight", "Weight"
    props = {"mass": 3.0}
    block = Linked(outputs=["mass"])
    def read(self):
        return {"mass": self.props["mass"]}

class Hook(paperboat.StationObject):
    type, label, role = "hook", "Hook", "joint"
    links = ["load"]
    block = Linked(outputs=["holds"])
    def __init__(self, props):
        super().__init__(props)
        self.load = None
    def link(self, slot, other):
        self.load = other
    def read(self):
        return {"holds": self.load.props["mass"] if self.load else 0.0}

paperboat.serve(paperboat.Backend(engine="test", descriptor="descriptor.toml", blocks=[Double, Square, Slide],
                                  objects=[Weight, Hook], state=World, dt=0.1, duration=5.0, emit_every=1))
'''

DESCRIPTOR = '''
domain = "t"
import = ["base", "step", "signal", "dashboard"]

[station]
objects = [{ id = "w", type = "weight" }, { id = "h", type = "hook", links = { load = "w" } }]

[graph]
nodes = [
  { id = "start", type = "start" }, { id = "s", type = "slide" },
  { id = "half", type = "constant", params = { value = 0.5 } },
  { id = "d", type = "double", params = { loud = true, name = "hello" } },
  { id = "q", type = "square-of-t" }, { id = "h", type = "hook" },
  { id = "doubled", type = "display" }, { id = "held", type = "display" }, { id = "squared", type = "display" },
]
edges = [
  { from = ["start", "next"], to = ["s", "in"] },
  { from = ["half", "y"], to = ["s", "curve"] },
  { from = ["half", "y"], to = ["d", "u"] },
  { from = ["d", "y"], to = ["doubled", "in"] },
  { from = ["h", "holds"], to = ["held", "in"] },
  { from = ["s", "progress"], to = ["q", "t"] },
  { from = ["q", "y"], to = ["squared", "in"] },
]
'''


def run_backend(tmp_path: Path, backend: str = BACKEND, descriptor: str = DESCRIPTOR, args: tuple[str, ...] = ()) -> list[dict]:
    (tmp_path / "server.py").write_text(textwrap.dedent(backend))
    (tmp_path / "descriptor.toml").write_text(textwrap.dedent(descriptor))
    env = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    proc = subprocess.run([sys.executable, "server.py", "--run", *args], cwd=tmp_path, env=env, capture_output=True,
                          text=True, timeout=120)
    assert proc.returncode == 0, proc.stderr
    return [json.loads(line) for line in proc.stdout.splitlines() if line.startswith("{")]


def test_a_python_backend_runs_on_the_engine(tmp_path: Path) -> None:
    msgs = run_backend(tmp_path)
    assert msgs[-1] == {"type": "run.finished", "run": "run-1", "status": "done"}
    last = {m["node"]: m["v"] for m in msgs if m["type"] == "signal"}
    assert last["doubled"] == 1.0          # a Block, its number param
    assert last["held"] == 3.0             # station objects linked to each other
    assert abs(last["squared"] - 1.0) < 1e-9  # a Generator fed a Timed step's progress (1 by its last frame)
    logs = [m for m in msgs if m["type"] == "log"]
    assert any(m.get("node") == "d" and m["text"] == "hello" for m in logs)  # ctx.log, its text param
    assert any(m["text"] == "x = 1.000" for m in logs)  # the Timed step followed its curve to 1


def test_variables_feed_params_and_take_var_overrides(tmp_path: Path) -> None:
    # #36: a param bound to a default variable; --var name=value sets it for the run.
    descriptor = DESCRIPTOR.replace('params = { loud = true, name = "hello" }',
                                    'params = { k = { var = "gain" }, loud = true, name = "hello" }')
    descriptor += '\n[[variables]]\nname = "gain"\nvalue = 3\n'
    doubled = lambda msgs: [m["v"] for m in msgs if m["type"] == "signal" and m["node"] == "doubled"][-1]
    assert doubled(run_backend(tmp_path, descriptor=descriptor)) == 1.5
    assert doubled(run_backend(tmp_path, descriptor=descriptor, args=("--var", "gain=4"))) == 2.0


def test_a_bad_definition_is_reported(tmp_path: Path) -> None:
    (tmp_path / "server.py").write_text(textwrap.dedent(BACKEND).replace('params = {"seconds": 1.0}', "params = {}"))
    (tmp_path / "descriptor.toml").write_text(textwrap.dedent(DESCRIPTOR))
    env = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    proc = subprocess.run([sys.executable, "server.py", "--run"], cwd=tmp_path, env=env, capture_output=True,
                          text=True, timeout=120)
    assert proc.returncode != 0
    assert "a Timed step needs a `seconds` param" in proc.stderr


LOOP_BACKEND = '''
import paperboat

class Memory(paperboat.Block):
    """Holds its input for a clock step: no feedthrough, so a loop through it is allowed."""
    type, inputs, outputs, feedthrough = "memory", ["u"], ["y"], False
    def __init__(self, params):
        super().__init__(params)
        self.x = 0.0
    def current(self):
        return {"y": self.x}
    def step(self, ctx, inputs):
        self.x = inputs.get("u", 0.0)
        return {}

class PlusOne(paperboat.Block):
    type, inputs, outputs = "plus-one", ["u"], ["y"]
    def step(self, ctx, inputs):
        return {"y": inputs.get("u", 0.0) + 1}

paperboat.serve(paperboat.Backend(engine="loop", descriptor="descriptor.toml", blocks=[Memory, PlusOne],
                                  dt=0.1, duration=1.0, emit_every=1))
'''

LOOP_DESCRIPTOR = '''
domain = "loop"
import = ["dashboard"]

[graph]
nodes = [{ id = "m", type = "memory" }, { id = "p", type = "plus-one" }, { id = "count", type = "display" }]
edges = [
  { from = ["m", "y"], to = ["p", "u"] },
  { from = ["p", "y"], to = ["m", "u"] },
  { from = ["m", "y"], to = ["count", "in"] },
]
'''


def test_a_python_block_without_feedthrough_closes_a_loop(tmp_path: Path) -> None:
    msgs = run_backend(tmp_path, LOOP_BACKEND, LOOP_DESCRIPTOR)
    assert msgs[-1]["status"] == "done"
    counts = [m["v"] for m in msgs if m["type"] == "signal" and m["node"] == "count"]
    assert counts == [float(k) for k in range(11)]  # one more each clock step, 0 to 10
