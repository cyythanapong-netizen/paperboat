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

VECTOR_BACKEND = '''
import paperboat
from paperboat import Vector, vector_port

class Position(paperboat.Block):
    type, label = "position", "Position"
    outputs = [vector_port("state", components=[("x", "m"), ("y", "m")])]
    def step(self, ctx, inputs):
        return {"state": Vector([2.0, 3.0])}

class AddCoordinates(paperboat.Block):
    type, label = "add-coordinates", "Add coordinates"
    inputs = [vector_port("state", components=[("horizontal", "m"), ("vertical", "m")])]
    outputs = ["total"]
    def step(self, ctx, inputs):
        x, y = inputs["state"]
        return {"total": x + y}

paperboat.serve(paperboat.Backend(engine="test-vectors", descriptor="descriptor.toml",
                                  blocks=[Position, AddCoordinates], mode="once"))
'''

VECTOR_DESCRIPTOR = '''
domain = "vectors"
import = ["dashboard"]
[graph]
nodes = [
  { id = "position", type = "position" },
  { id = "add", type = "add-coordinates" },
  { id = "show", type = "display" },
  { id = "vector-show", type = "display" },
]
edges = [
  { from = ["position", "state"], to = ["add", "state"] },
  { from = ["add", "total"], to = ["show", "in"] },
  { from = ["position", "state"], to = ["vector-show", "in"] },
]
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


def run_backend(tmp_path: Path, backend: str = BACKEND, descriptor: str = DESCRIPTOR,
                args: tuple[str, ...] = (), success: bool = True) -> list[dict]:
    (tmp_path / "server.py").write_text(textwrap.dedent(backend))
    (tmp_path / "descriptor.toml").write_text(textwrap.dedent(descriptor))
    env = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    proc = subprocess.run([sys.executable, "server.py", "--run", *args], cwd=tmp_path, env=env, capture_output=True,
                          text=True, timeout=120)
    assert (proc.returncode == 0) == success, proc.stderr
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


def test_a_python_vector_uses_one_typed_wire_with_component_metadata(tmp_path: Path) -> None:
    msgs = run_backend(tmp_path, backend=VECTOR_BACKEND, descriptor=VECTOR_DESCRIPTOR)
    signal = next(m for m in msgs if m["type"] == "signal" and m["node"] == "show")
    assert signal["v"] == 5.0
    vector_signal = next(m for m in msgs if m["type"] == "signal" and m["node"] == "vector-show")
    assert vector_signal["v"] == [2.0, 3.0]
    assert vector_signal["components"] == [{"id": "x", "unit": "m"}, {"id": "y", "unit": "m"}]


def test_a_python_vector_must_match_its_declared_dimension(tmp_path: Path) -> None:
    backend = VECTOR_BACKEND.replace(
        'vector_port("state", components=[("x", "m"), ("y", "m")])',
        'vector_port("state", 3)',
        1,
    )
    backend = backend.replace(
        'inputs = [vector_port("state", components=[("horizontal", "m"), ("vertical", "m")])]',
        'inputs = [vector_port("state")]',
    )
    msgs = run_backend(tmp_path, backend=backend, descriptor=VECTOR_DESCRIPTOR, success=False)
    assert msgs[-1]["type"] == "run.finished" and msgs[-1]["status"] == "failed"
    assert any(m.get("node") == "position" and "expected 3" in m.get("text", "") for m in msgs)


def test_python_vector_component_units_are_checked_on_a_wire(tmp_path: Path) -> None:
    backend = VECTOR_BACKEND.replace(
        '("horizontal", "m"), ("vertical", "m")',
        '("horizontal", "cm"), ("vertical", "m")',
    )
    msgs = run_backend(tmp_path, backend=backend, descriptor=VECTOR_DESCRIPTOR, success=False)
    assert msgs[-1]["type"] == "run.finished" and msgs[-1]["status"] == "failed"
    assert any("vector component 0 in m" in m.get("text", "") for m in msgs if m["type"] == "log")


def test_a_blocks_view_is_checked(tmp_path: Path) -> None:
    anchor = 'inputs, outputs = ["u"], ["y"]\n    params = {"k": 2.0,'

    def backend(view: str) -> str:
        return BACKEND.replace(anchor, f'inputs, outputs = ["u"], ["y"]\n    view = {view}\n    params = {{"k": 2.0,')

    msgs = run_backend(tmp_path, backend('{"compact": True, "glyph": "gain", "value": "{k}"}'))
    assert msgs[-1]["status"] == "done"  # a view is only a hint: the block runs as before

    (tmp_path / "server.py").write_text(textwrap.dedent(backend('{"glyf": "gain"}')))
    (tmp_path / "descriptor.toml").write_text(textwrap.dedent(DESCRIPTOR))
    env = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    proc = subprocess.run([sys.executable, "server.py", "--run"], cwd=tmp_path, env=env, capture_output=True,
                          text=True, timeout=120)
    assert proc.returncode != 0
    assert 'view has no key "glyf"' in proc.stderr


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


MODEL_BACKEND = '''
import math
import paperboat
from paperboat import Linked, Param, scene

class Mass(paperboat.Model):
    """x'' = (force - k x) / m, stepped by the engine's solver."""
    type, label = "mass", "Mass on a spring"
    props = {"k": 4.0, "x0": 1.0, "solver": Param("rk4", "enum", paperboat.solve.names())}
    block = Linked(inputs=["force"], outputs=["x"])

    def initial(self):
        return [self.props["x0"], 0.0]

    def derivatives(self, t, x, inputs):
        return [x[1], inputs.get("force", 0.0) - self.props["k"] * x[0]]

    def read(self):
        return {"x": self.x[0]}

    def entities(self, id, z):
        return [scene.rect(f"{id}/mass", self.x[0], 0, 0.4, 0.4, "#2f5d62", z=z),
                scene.spring(f"{id}/spring", (-2, 0), (self.x[0] - 0.2, 0), coils=6)]

paperboat.serve(paperboat.Backend(engine="model", descriptor="descriptor.toml", objects=[Mass],
                                  dt=0.01, duration=1.0, emit_every=100))
'''

MODEL_DESCRIPTOR = '''
domain = "model"
import = ["dashboard"]

[station]
objects = [{ id = "m", type = "mass", props = { solver = "SOLVER" } }]

[graph]
nodes = [{ id = "m", type = "mass" }, { id = "x", type = "display" }]
edges = [{ from = ["m", "x"], to = ["x", "in"] }]
'''


def test_a_python_model_is_stepped_by_the_engines_solver(tmp_path: Path) -> None:
    import math
    for solver, tolerance in (("rk4", 1e-7), ("rk45", 1e-5), ("euler", 0.1)):
        msgs = run_backend(tmp_path, MODEL_BACKEND, MODEL_DESCRIPTOR.replace("SOLVER", solver))
        assert msgs[-1]["status"] == "done", msgs
        x = [m["v"] for m in msgs if m["type"] == "signal" and m["node"] == "x"][-1]
        # The display shows x before the last step: t = 0.99 at the run's last pass (ω = 2).
        assert abs(x - math.cos(2 * 1.0)) < tolerance or abs(x - math.cos(2 * 0.99)) < tolerance, (solver, x)
        scene = [m for m in msgs if m["type"] == "scene"][0]["entities"]
        ids = {e["id"] for e in scene}
        assert "m/mass" in ids and "m/spring/0" in ids  # scene builders, a spring's lines flattened in


def test_scene_and_solve_are_bound() -> None:
    sys.path.insert(0, str(ROOT / "python"))
    import paperboat
    rod = paperboat.scene.rod("r", (0, 0), (0, 2), 0.1)
    assert rod.pos == [0.0, 1.0, 0.0] and abs(rod.size[0] - 2) < 1e-12
    x = paperboat.solve.step("rk4", lambda t, x: [x[1], -x[0]], [1.0, 0.0], 0.0, 0.1)
    assert abs(x[0] - 0.9950041666666667) < 1e-15
    try:
        paperboat.solve.Solver("ode23")
    except ValueError as err:
        assert "rk4, rk45" in str(err)
    else:
        raise AssertionError("an unknown solver must be refused")


ELEMENTWISE_BACKEND = '''
import paperboat

class Scale(paperboat.Block):
    type, label = "scale", "Scale"
    inputs, outputs = ["u"], ["y"]
    params = {"k": 10}
    elementwise = True  # written for one number; the engine runs it per element of a vector
    def step(self, ctx, inputs):
        return {"y": inputs.get("u", 0.0) * self.params["k"]}

paperboat.serve(paperboat.Backend(engine="test-elementwise", descriptor="descriptor.toml", blocks=[Scale], mode="once"))
'''

ELEMENTWISE_DESCRIPTOR = '''
domain = "ew"
import = ["base", "dashboard"]
[graph]
nodes = [
  { id = "one", type = "constant", params = { value = 1 } }, { id = "two", type = "constant", params = { value = 2 } },
  { id = "m", type = "mux", params = { names = "p q" } }, { id = "s", type = "scale" },
  { id = "n", type = "scale" }, { id = "show", type = "display" }, { id = "num", type = "display" },
]
edges = [
  { from = ["one", "y"], to = ["m", "p"] }, { from = ["two", "y"], to = ["m", "q"] },
  { from = ["m", "y"], to = ["s", "u"] }, { from = ["s", "y"], to = ["show", "in"] },
  { from = ["two", "y"], to = ["n", "u"] }, { from = ["n", "y"], to = ["num", "in"] },
]
'''


def test_an_elementwise_python_block_takes_a_vector_and_stays_scalar_on_numbers(tmp_path: Path) -> None:
    msgs = run_backend(tmp_path, backend=ELEMENTWISE_BACKEND, descriptor=ELEMENTWISE_DESCRIPTOR)
    last = {m["node"]: m for m in msgs if m["type"] == "signal"}
    assert last["show"]["v"] == [10.0, 20.0]
    assert [c["id"] for c in last["show"]["components"]] == ["p", "q"]
    assert last["num"]["v"] == 20.0  # numbers in: a number out, as before


TUNER_BACKEND = '''
import paperboat
from paperboat import Param

class Tuner(paperboat.Block):
    type, label = "tuner", "Tuner"
    outputs = ["seen"]
    params = {"target": Param("g", "variable")}
    def step(self, ctx, inputs):
        if ctx.t >= 0.5:
            assert ctx.set_variable(self.params["target"], 4.0)
            assert not ctx.set_variable("h", 1.0)  # not tunable
        return {"seen": ctx.variable("g")}

paperboat.serve(paperboat.Backend(engine="test-tuner", descriptor="descriptor.toml", blocks=[Tuner],
                                  dt=0.1, duration=1.0, emit_every=1))
'''

TUNER_DESCRIPTOR = '''
domain = "tuner"
import = ["dashboard"]
[[variables]]
name = "g"
value = 2
tunable = true
[[variables]]
name = "h"
value = 3
[graph]
nodes = [{ id = "tuner", type = "tuner" }, { id = "show", type = "display" }]
edges = [{ from = ["tuner", "seen"], to = ["show", "in"] }]
'''


def test_a_python_block_reads_and_sets_a_tunable_variable(tmp_path: Path) -> None:
    # #166: ctx.set_variable lands at the end of the pass; the UI is told with variables.patch.
    msgs = run_backend(tmp_path, backend=TUNER_BACKEND, descriptor=TUNER_DESCRIPTOR)
    seen = [m["v"] for m in msgs if m["type"] == "signal" and m["node"] == "show"]
    assert seen[0] == 2.0 and seen[-1] == 4.0
    assert [m["values"] for m in msgs if m["type"] == "variables.patch"] == [{"g": 4.0}]


def test_solve_ode_scalar_and_list():
    import math
    import paperboat

    # Exponential decay dx/dt = -x, x0 = 1.0
    ode = paperboat.solve.ode(lambda x, t: [-x[0]], [1.0], solver="rk4")
    assert ode.solver == "rk4"
    assert ode.t == 0.0
    assert ode.x == [1.0]

    dt = 0.01
    for _ in range(100):
        ode.step(dt)

    assert abs(ode.t - 1.0) < 1e-12
    assert abs(ode.x[0] - math.exp(-1.0)) < 1e-6

    # Test reset state
    ode.x = [2.0]
    assert ode.x == [2.0]


def test_solve_ode_numpy_and_rk45():
    import numpy as np
    import paperboat

    # Harmonic oscillator: dx/dt = v, dv/dt = -x
    def f(state, t):
        x, v = state[0], state[1]
        return np.array([v, -x])

    x0 = np.array([0.0, 1.0])
    ode = paperboat.ode(f, x0, solver="rk45")
    dt = 0.01
    for _ in range(100):
        res = ode.step(dt)

    assert isinstance(res, np.ndarray)
    # x(t) = sin(t), at t=1.0: sin(1) approx 0.84147, cos(1) approx 0.54030
    assert abs(res[0] - np.sin(1.0)) < 1e-4
    assert abs(res[1] - np.cos(1.0)) < 1e-4


def test_solve_ode_step_with_explicit_time_and_euler():
    import paperboat

    # dx/dt = 2*t, x(0) = 0
    ode = paperboat.solve.ode(lambda x, t: [2.0 * t], [0.0], solver="euler")
    dt = 0.1
    for k in range(10):
        ode.step(k * dt, dt)

    assert ode.solver == "euler"


def test_backend_with_solver_setting():
    import paperboat

    b = paperboat.Backend(engine="test-engine", descriptor="descriptor.toml", solver="rk45")
    assert b.solver == "rk45"


def test_solve_helper_functions():
    import paperboat

    s_rk4 = paperboat.solve.rk4(lambda x: [-x[0]], [1.0])
    assert s_rk4.solver == "rk4"
    s_rk45 = paperboat.solve.rk45(lambda x: [-x[0]], [1.0])
    assert s_rk45.solver == "rk45"
    s_rk2 = paperboat.solve.rk2(lambda x: [-x[0]], [1.0])
    assert s_rk2.solver == "rk2"
    s_euler = paperboat.solve.euler(lambda x: [-x[0]], [1.0])
    assert s_euler.solver == "euler"
    s_disc = paperboat.solve.discrete(lambda x: [x[0] * 0.5], [1.0])
    assert s_disc.solver == "discrete"
    s_disc.step(0.1)
    assert abs(s_disc.x[0] - 0.5) < 1e-12

