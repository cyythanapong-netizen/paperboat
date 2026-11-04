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

from ._engine import Context, Handle, make, options

__all__ = [
    "Backend", "Block", "Context", "DomainState", "Generator", "Handle", "Linked", "Monitor", "Param", "Port",
    "StationObject", "Step", "Timed", "serve",
]


class Port(NamedTuple):
    """A port with a type other than "signal" or a unit: Port("points", "points"),
    Port("in", "step"), Port("force", unit="N"). "any" takes any value but a step; with a unit
    on both ends a wire's units must agree."""

    id: str
    type: str = "signal"
    unit: str = ""


class Param(NamedTuple):
    """A param with a type its default doesn't say: Param("Space", "key"), Param("data/a.png",
    "file"), Param("fast", "enum", ["slow", "fast"]) (a dropdown; the block gets the name in
    its params, and an expression its index). Plain defaults say it: a number, a bool (a
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
    inputs: list[str | Port] = []
    outputs: list[str | Port] = []
    params: dict[str, float | Param] = {}  # the defaults; an instance's self.params holds its values
    # False for a block whose outputs this step don't depend on its inputs this step (an
    # integrator, a delay): implement current() (its outputs, from its state), and step()
    # then takes the inputs after the pass. A feedback loop may then pass through it.
    feedthrough: bool = True

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

    def entities(self, id: str, z: float) -> list[dict]:
        """Its shapes, ids prefixed with `id` + "/", at depth `z`."""
        return []

    def link(self, slot: str, other: "StationObject | None") -> None:
        """Another station object fills one of its `links` slots (once, before a run).
        `other` is None for a toolkit (C++) object."""


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
    port: int = 8765


def serve(backend: Backend) -> None:
    """The backend's command line (the engine's): serve on `--port N` until Ctrl+C, or with
    `--run`, run the starter once headless, print each protocol message as a JSON line and
    exit (0 when the run is done)."""
    try:
        port, run, variables = options(sys.argv, backend.port)
    except ValueError as err:
        print(err, file=sys.stderr)
        sys.exit(2)
    hosted = make(backend.engine, str(backend.descriptor), list(backend.blocks), list(backend.objects), backend.state,
                  backend.mode, backend.dt, backend.duration, backend.emit_every, backend.view, backend.speed)
    if run:
        try:
            status = hosted.run_starter(variables)
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
