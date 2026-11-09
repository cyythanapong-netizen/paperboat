"""Paperboat for Python: write a backend's domain code in Python on the C++ engine.

The engine (C++, bound as `paperboat._engine`) does everything a backend does: it loads
the descriptor, checks and runs graphs (Signal flow on a clock, Step flow, run-once),
runs the toolkit's blocks and serves the protocol. Python adds only the domain's own
code, as subclasses of the classes below. A class's attributes define it (what the UI
shows); its methods are what the engine calls:

    class Jump(paperboat.Block):
        type = "jump"                       # its id in the catalog
        label = "Jump"
        kind = "source"                     # source | probe | function | monitor
        inputs = ["trigger"]
        params = {"strength": 10}           # defaults; the instance gets the values

        def step(self, ctx, inputs):        # inputs: {"trigger": 1.0} → outputs dict
            if inputs.get("trigger", 0) > 0.5:
                ctx.state.jump(self.params["strength"])
                ctx.log("jump!")             # to the Console, tagged with this block
            return {}

    paperboat.serve(paperboat.Backend(engine="runner", descriptor=…, blocks=[Jump, …],
                                      state=Game, port=8768))

The blocks become the domain's package (named after the descriptor's `domain`), so the
palette has them without the descriptor listing them. Reference: docs/api (Python API).

Never implement a framework feature here: add it to the engine (engine/) and bind it.
"""

import os
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, NamedTuple

if sys.version_info < (3, 11):
    raise ImportError(f"paperboat needs Python 3.11 or later, not {sys.version.split()[0]} ({sys.executable}); "
                      "set PAPERBOAT_PYTHON to a newer python3 for the paperboat command")
try:
    from ._engine import Context, Entity, Handle, make, options, remember_args, scene, solve
except ImportError as err:
    # The packages ship the module for CPython 3.11 and, through the stable ABI, for 3.12 and later (pb#223).
    raise ImportError(f"paperboat's engine module (paperboat._engine) does not load in Python {sys.version.split()[0]} "
                      f"({sys.executable}): {err}. The packages have it for CPython 3.11 and later "
                      "(PAPERBOAT_PYTHON picks the interpreter); a checkout builds it with npm run build:python") from err

Vector = list[float]

__all__ = [
    "Backend", "Block", "Context", "DomainState", "Entity", "Generator", "Handle", "Linked", "Model", "Monitor", "ODE", "Param",
    "Port", "StationObject", "Step", "Timed", "Vector", "VectorComponent", "numbers_port", "ode", "points_port", "scene", "serve", "solve",
    "vector_port",
]


class VectorComponent(NamedTuple):
    """One ordered vector element's label and optional unit."""

    id: str
    unit: str = ""


class Port(NamedTuple):
    """A typed port, optionally sized: Port("points", "points"), Port("in", "step"),
    Port("force", unit="N"). Vector ports may declare a dimension or component labels/units.
    "any" takes any value but a step; units must agree where both ends declare them."""

    id: str
    type: str = "signal"
    unit: str = ""
    dimension: int | None = None
    components: tuple[VectorComponent, ...] = ()


def vector_port(id: str, dimension: int | None = None, components=()) -> Port:
    """A numeric vector port. Components may be names, (name, unit) pairs, or VectorComponents.

    Example: ``vector_port("state", components=[("x", "m"), ("angle", "rad")])``.
    """
    parts = tuple(
        component if isinstance(component, VectorComponent)
        else VectorComponent(component) if isinstance(component, str)
        else VectorComponent(*component)
        for component in components
    )
    if dimension is not None and dimension < 1:
        raise ValueError("a vector dimension must be positive")
    if parts:
        if dimension is not None and dimension != len(parts):
            raise ValueError(f"vector dimension {dimension} does not match {len(parts)} components")
        dimension = len(parts)
    return Port(id, "vector", dimension=dimension, components=parts)


def numbers_port(id: str, unit: str = "") -> Port:
    """A list of numbers (#24), any length, in one unit: a block gives it as a list of floats.
    It joins vector ports and elementwise blocks' signal ports (which then map over it)."""
    return Port(id, "numbers", unit=unit)


def points_port(id: str, coordinates=()) -> Port:
    """A list of points (#24): a block gives it as a list of rows, ``[[x, z], ...]``. The
    coordinates (names, (name, unit) pairs, or VectorComponents) fix each point's, by position.

    Example: ``points_port("profile", [("x", "m"), ("z", "m")])``.
    """
    parts = vector_port("_", components=coordinates).components if coordinates else ()
    return Port(id, "points", dimension=len(parts) or None, components=parts)


class Param(NamedTuple):
    """A param with a type its default doesn't say: Param("Space", "key"), Param("data/a.png",
    "file"), Param("fast", "enum", ["slow", "fast"]) (a dropdown; the block gets the name in
    its params, and an expression its index), Param("Kp", "variable") (a picker of the project's
    variables; the block gets the name, to read with ctx.variable or write with ctx.set_variable).
    Plain defaults say it: a number, a bool (a
    "boolean", a checkbox), text ("text")."""

    default: float | str
    type: str = "number"
    options: tuple | list = ()


class Block:
    """A signal block: outputs from inputs, once per pass. `ctx.state` is the domain state."""

    _paperboat = "block"
    type: str = ""                       # its id in the catalog (required)
    label: str = ""                      # shown on the block (default: type)
    kind: str = "function"               # source | probe | function | monitor
    description: str = ""                # one line: what it does; shown on its card in the browser
    # How the UI draws it (a hint; absent: the normal card). A small plate with a glyph and
    # a caption: {"compact": True, "glyph": "gain", "value": "{k}"}. glyph is a curve name
    # (gain, plus, step, clamp, const) or a few characters; value is an expression of params.
    view: dict = {}
    inputs: list[str | Port] = []
    outputs: list[str | Port] = []
    params: dict[str, float | Param] = {}  # the defaults; an instance's self.params holds its values
    # False for a block whose outputs this step don't depend on its inputs this step (an
    # integrator, a delay): implement current() (its outputs, from its state), and step()
    # then takes the inputs after the pass. A feedback loop may then pass through it.
    feedthrough: bool = True
    # A variable number of inputs: the name of a text param ("signs"); the block has one input
    # per sign character in its value ("+-+": a, b, c; 1 to 26), and `inputs` is only the default.
    ports_from: str = ""
    # The name of a text param whose expression's free names are the block's inputs ("f").
    inputs_from: str = ""
    # The name of a text param of names ("x v theta") that are the block's outputs.
    outputs_from: str = ""
    # True for a block that works on numbers one at a time: the engine lifts it to vectors.
    # Its signal ports also take a vector wire (scalars broadcast); with a vector in it runs
    # as one instance per element, each with its own self.params and state, and each output
    # is gathered into a vector. step() still sees floats.
    elementwise: bool = False
    # True for a block whose outputs depend on nothing but its params, its inputs and the
    # variables it reads through ctx.variable() (no clock, keys, ctx.state, randomness, files
    # or other side effects): a backend with mode="once" then reuses its outputs from the last
    # run while none of those changed (its node.state is then "done" with no "running"). None or
    # False: called every run.
    cacheable: bool | None = None

    def __init__(self, params: dict) -> None:
        self.params = params

    def step(self, ctx: Context, inputs: dict) -> dict:
        return {}

    def current(self) -> dict:
        """A block without feedthrough: what it outputs now, from its state alone."""
        return {}


class Generator(Block):
    """A signal f(t): implement value(t). t is the block's input `t` when wired (a step's
    `progress`, say), else the run clock; the engine does that, and outputs value(t)."""

    _paperboat = "generator"
    inputs = ["t"]
    outputs = ["y"]

    def value(self, t: float) -> float:
        return 0.0


class Monitor(Block):
    """Shows its input on the Dashboard as an instrument (graph, display, image). Override
    step() to show something else: return {"value": …}."""

    kind = "monitor"
    inputs = ["in"]
    instrument: str = "graph"


class Step:
    """A block on a Step flow's path: enter() when a path arrives, then tick() each clock
    step until it returns the step output to leave by ("" to stay)."""

    _paperboat = "step"
    type: str = ""
    label: str = ""
    kind: str = "function"
    inputs: list[str | Port] = [Port("in", "step")]
    outputs: list[str | Port] = [Port("next", "step")]
    params: dict[str, float | Param] = {}

    def __init__(self, params: dict) -> None:
        self.params = params

    def enter(self, ctx: Context, inputs: dict) -> None: ...

    def tick(self, ctx: Context, inputs: dict) -> str:
        return "next"

    def may_start(self) -> bool:
        """For a hat (no step input): whether it may still start a path."""
        return False

    def ends_run(self) -> bool:
        return False

    def current(self) -> dict:
        """Its signal outputs now (ports of a type other than "step"), such as a progress."""
        return {}


class Timed(Step):
    """A step that does its work over `seconds`: implement advance(ctx, start, end), which
    moves the work from progress `start` to `end` (0 to 1). The engine follows the optional
    `curve` input (0 to 1) when wired, evenly otherwise, ends at 1, and outputs `progress`."""

    _paperboat = "timed"
    inputs = [Port("in", "step"), "curve"]
    outputs = [Port("next", "step"), "progress"]
    params = {"seconds": 1}

    def begin(self, ctx: Context, inputs: dict) -> None:
        """A path arrived."""

    def advance(self, ctx: Context, start: float, end: float) -> None:
        """Move the work from progress `start` to `end`."""


class Linked(NamedTuple):
    """The block a station object spawns on the Flow page, through which a flow acts on it."""

    inputs: tuple = ()
    outputs: tuple = ()
    kind: str = "probe"
    label: str = ""
    view: dict | None = None


class StationObject:
    """A thing on the Station page (a body, a joint, a photo): owns its state and its
    properties, which are edited on the Station page only (ISA-88), steps with the run
    clock and draws itself."""

    _paperboat = "object"
    type: str = ""
    label: str = ""
    role: str = "body"                   # body | joint
    description: str = ""                # one line: what it is; shown on its card in the browser
    props: dict[str, float | Param] = {}  # the defaults; an instance's self.props holds its values
    links: list[str] = []                # slots that name other station objects
    block: Linked | None = None          # its linked block, if a flow acts on it

    def __init__(self, props: dict) -> None:
        self.props = props

    def read(self) -> dict:
        """The linked block's outputs, from the current state."""
        return {}

    def step(self, ctx: Context, inputs: dict) -> None:
        """Advance one clock step under the linked block's inputs."""

    def entities(self, id: str, z: float) -> list:
        """Its shapes (paperboat.scene.rect(…), or dicts), ids prefixed with `id` + "/", at depth `z`."""
        return []

    def link(self, slot: str, other: "StationObject | None") -> None:
        """Another station object fills one of its `links` slots (once, before a run).
        `other` is None for a toolkit (C++) object."""


class Model(StationObject):
    """A station object that is a system of differential equations: give its initial state
    and derivatives(); a solver (the class's `solver`, or a `solver` prop) advances it each
    clock step, and `self.x` is the state now, for read() and entities().

        class Mass(paperboat.Model):
            type, label = "mass", "Mass on a spring"
            props = {"k": 4.0, "x0": 1.0}
            block = paperboat.Linked(inputs=["force"], outputs=["x", "v"])

            def initial(self):
                return [self.props["x0"], 0.0]          # position, velocity

            def derivatives(self, t, x, inputs):
                return [x[1], inputs.get("force", 0.0) - self.props["k"] * x[0]]

            def read(self):
                return {"x": self.x[0], "v": self.x[1]}
    """

    _paperboat = "model"
    solver: str = "rk4"                  # rk4 | rk45 | rk2 | euler | discrete (paperboat.solve.names())
    x: Vector = []                       # the state now, set by the engine

    def initial(self) -> Vector:
        """The state when a run starts."""
        return []

    def derivatives(self, t: float, x: Vector, inputs: dict) -> Vector:
        """dx/dt at time t in state x, under the linked block's inputs (for "discrete": the next x)."""
        return [0.0] * len(x)


class ODE:
    """An ODE system dx/dt = f(t, x) integrated with a built-in solver (rk4, rk45, rk2, euler, discrete)."""

    def __init__(self, f: Callable, x0, solver: str = "rk4", **options):
        self._f = f
        self.solver = solver
        self._solver = solve.Solver(solver, **options)
        self._is_numpy = hasattr(x0, "__array__")
        self.x = list(x0)
        self.t = 0.0

    def step(self, t_or_dt: float, dt: float | None = None):
        """Advance the ODE state by one step. Can be called as step(t, dt) or step(dt)."""
        if dt is None:
            t = self.t
            step_dt = float(t_or_dt)
        else:
            t = float(t_or_dt)
            step_dt = float(dt)
            self.t = t

        def wrapped_f(at: float, current_x: list[float]):
            if self._is_numpy:
                try:
                    import numpy as np
                    val = self._f(at, np.array(current_x))
                    if hasattr(val, "tolist"):
                        return val.tolist()
                    return list(val)
                except ImportError:
                    pass
            val = self._f(at, current_x)
            if hasattr(val, "tolist"):
                return val.tolist()
            return list(val)

        res = self._solver.step(wrapped_f, self.x, t, step_dt)
        self.t = t + step_dt
        if self._is_numpy:
            try:
                import numpy as np
                self.x = np.array(res)
            except ImportError:
                self.x = res
        else:
            self.x = res
        return self.x


def ode(f: Callable, x0, solver: str = "rk4", **options) -> ODE:
    """Bootstrap an ODE system with a built-in solver. Returns an ODE object with .step(t, dt)."""
    return ODE(f, x0, solver=solver, **options)


solve.ode = ode
solve.rk4 = lambda f, x0, **options: ode(f, x0, solver="rk4", **options)
solve.rk45 = lambda f, x0, **options: ode(f, x0, solver="rk45", **options)
solve.rk2 = lambda f, x0, **options: ode(f, x0, solver="rk2", **options)
solve.euler = lambda f, x0, **options: ode(f, x0, solver="euler", **options)
solve.discrete = lambda f, x0, **options: ode(f, x0, solver="discrete", **options)


class DomainState:
    """What a domain's blocks share during a run (a game, a document, a physical system).
    The engine makes one per run, steps it after each clocked pass and draws it."""

    def entities(self) -> list[dict]:
        """Every entity it draws: protocol Entity dicts (id, shape, pos, size, color…)."""
        return []

    def patch(self) -> list[dict]:
        """The entities that change (default: all)."""
        return self.entities()

    def step(self, ctx: Context) -> str | None:
        """Advance after a clocked pass; a message ends the run."""
        return None

    def summary(self) -> str | None:
        """Logged when a run ends."""
        return None


@dataclass
class Backend:
    engine: str                                  # the name in `hello`
    descriptor: Path                             # its descriptor.toml
    blocks: list[type] = field(default_factory=list)   # the domain's Block, Monitor and Step classes
    objects: list[type] = field(default_factory=list)  # the domain's StationObject classes
    state: Callable[[], DomainState] | None = None     # a fresh domain state per run
    mode: str = "clock"                          # clock (Signal or Step flow) | once (evaluate once)
    dt: float = 0.01                             # clock step, s
    duration: float = 10.0                       # run length (Step flow: its limit), s
    emit_every: int = 4                          # send signals and scene patches every N steps
    speed: float = 1.0                           # times real time (0: as fast as it can); the UI may change it per run
    view: dict | None = None                     # the scene's camera hint, e.g. {"mode": "2d", …}
    connectors: dict | None = None               # Modelica connector definitions (§4), keyed by short name
    port: int = 8765
    solver: str = ""                             # default solver: rk4 | rk45 | rk2 | euler | discrete ("": backend.toml's, else rk4)


def serve(backend: Backend) -> None:
    """The backend's command line (the engine's): serve on `--port N` until Ctrl+C, or with
    `--run`, run the starter once headless, print each protocol message as a JSON line and
    exit (0 when the run is done)."""
    remember_args([sys.executable, *sys.argv])
    try:
        port, run, variables, duration = options(sys.argv, backend.port)
    except ValueError as err:
        print(err, file=sys.stderr)
        sys.exit(2)
    hosted = make(backend.engine, str(backend.descriptor), list(backend.blocks), list(backend.objects), backend.state,
                  backend.mode, backend.dt, backend.duration, backend.emit_every, backend.view, backend.speed, backend.solver,
                  backend.connectors)
    if run:
        try:
            status = hosted.run_starter(variables, duration)
        except ValueError as err:  # a --var the backend doesn't have
            print(err, file=sys.stderr)
            sys.exit(2)
        sys.stdout.flush()
        os._exit(0 if status == "done" else 1)
    hosted.listen(port)
    print(f"paperboat backend ({backend.engine}) on ws://localhost:{port}", flush=True)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        hosted.stop()
        os._exit(0)  # the engine's threads are done; skip interpreter teardown
