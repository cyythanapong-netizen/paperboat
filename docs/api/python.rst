Python API
==========

.. py:module:: paperboat

``paperboat`` binds the C++ engine (``paperboat._engine``, nanobind). The engine does
everything a backend does; Python provides only the domain's own code, as subclasses of
the classes below. **A class's attributes define it** (what the UI shows), **its methods
are what the engine calls**. The classes a backend passes become the domain's package
(named after the descriptor's ``domain``), so the palette has them without the descriptor
listing them.

Install: nothing to do with a paperboat package (.deb, .rpm, .tar.gz, .zip, npm): it ships the
module in ``lib/paperboat/python`` and ``paperboat run`` / ``check`` put it on ``PYTHONPATH``. It is
built for CPython 3.11 and, through the stable ABI (abi3), for every CPython from 3.12 on; the
interpreter is the backend's ``.venv``, else ``$PAPERBOAT_PYTHON``, else ``python3``. Elsewhere:
``pip install`` a release wheel, or ``pip install ./python`` (or ``npm run build:python`` and
``PYTHONPATH=python`` while developing). Never implement a framework feature here: add it to the
engine and bind it.

API Quick Reference
-------------------

.. list-table::
   :header-rows: 1
   :widths: 22 28 50

   * - Category
     - Symbol
     - Description
   * - **Ports & Definitions**
     - :py:class:`Port`
     - Input/output port with data type, dimensions, and unit annotations.
   * -
     - :py:class:`VectorComponent`
     - Label and engineering unit for an ordered vector channel.
   * -
     - :py:func:`vector_port`
     - Factory helper constructing typed vector ports with named channels.
   * -
     - :py:class:`Param`
     - Parameter declaration (number, key, enum dropdown, file).
   * -
     - :py:class:`Linked`
     - Association between a station object and its Flow linked block.
   * - **Blocks**
     - :py:class:`Block`
     - Base computational block evaluated each clock step.
   * -
     - :py:class:`Generator`
     - Signal generator computing :math:`f(t)` from time and parameters.
   * -
     - :py:class:`Monitor`
     - Telemetry instrument piping data to Board scopes and readouts.
   * -
     - :py:class:`Step`
     - Step flow node executing sequential procedural actions.
   * -
     - :py:class:`Timed`
     - Step block that runs for a configured duration before moving to next.
   * - **Station & Geometry**
     - :py:class:`StationObject`
     - Physical domain entity with properties, links, and visual shapes.
   * -
     - :py:class:`Entity`
     - 3D/2D visual scene shape rendered on the Station page.
   * -
     - :py:func:`scene.box`, :py:func:`scene.sphere`, :py:func:`scene.line`, :py:func:`scene.rect`, :py:func:`scene.circle`, :py:func:`scene.image`, :py:func:`scene.rod`, :py:func:`scene.spring`
     - 3D and 2D visual geometry entity factory functions.
   * - **Models & Solvers**
     - :py:class:`Model`
     - Continuous plant differential equation ODE model.
   * -
     - :py:func:`solve.step`
     - Step a differential equation solver forward by :math:`\Delta t`.
   * -
     - :py:class:`solve.Solver`
     - Configured numerical ODE integrator (e.g. RK4).
   * -
     - :py:class:`Context`
     - Runtime context providing simulation time ``t``, step ``dt``, and logging.
   * - **Domain & Server**
     - :py:class:`DomainState`
     - Global domain-level state spanning multiple blocks.
   * -
     - :py:class:`Handle`
     - Opaque data handle passed across flow wires.
   * -
     - :py:class:`Backend`
     - Programmatically defined domain backend.
   * -
     - :py:func:`serve`
     - Launch the WebSocket/HTTP server on a designated port.

Definitions
-----------

.. py:class:: Port(id, type="signal", unit="", dimension=None, components=())

    A port with a type other than ``"signal"``: ``Port("points", "points")``,
    ``Port("in", "step")``. Plain strings are signal ports. A ``"vector"`` port carries
    an ordered list of numbers on one wire. ``dimension`` declares a fixed length;
    ``components`` can name the entries and their units.

.. py:class:: VectorComponent(id, unit="")

    One ordered vector component's label and optional unit. A component can also be written
    as a name or ``(name, unit)`` pair.

.. py:function:: vector_port(id, dimension=None, components=())

    Make a vector :py:class:`Port`. For example::

       outputs = [vector_port("state", components=[("x", "m"), ("angle", "rad")])]

    The port's ordered components travel together; names are descriptive, while known
    dimensions and units are checked on the wire. Use ``paperboat.Vector`` (an alias for
    ``list[float]``) to annotate a block's numeric vector values.

.. py:class:: Param(default, type="number", options=())

   A param (or property) whose type its default doesn't say: ``Param("Space", "key")``,
   ``Param("data/a.png", "file")``, ``Param("fast", "enum", ["slow", "fast"])`` (a dropdown:
   the block gets the name). Plain defaults say it: a number (``number``), a bool
   (``boolean``, a checkbox, 1.0 or 0.0 to the block), text (``text``). ``Param("Kp",
   "variable")`` names a project variable (a picker in the UI): the block gets the name, to
   pass to ``ctx.variable`` or ``ctx.set_variable``.

.. py:class:: Linked(inputs=(), outputs=(), kind="probe", label="")

   A station object's linked block: the block on the Flow page through which a flow acts
   on it.

Blocks
------

.. py:class:: Block(params)

   A signal block: outputs from inputs, once per pass.

   .. py:attribute:: description
      :type: str

      One line in plain words: what it does. Shown on its card in the block browser.

   .. py:attribute:: view
      :type: dict

      How the UI draws the block, a hint (absent: the normal card). Keys, all optional:
      ``compact`` (a small plate with bare port dots and a caption), ``glyph`` (a few
      characters drawn as text; a curve is a ``view.icon``)
      and ``value`` (an expression of the params for the caption, such as ``"{k}"``).

   .. py:attribute:: ports_from
      :type: str

      The name of a text param that decides the block's inputs: one per sign character in its
      value (``"+-+"``: ids ``a``, ``b``, ``c``; at most 26); ``inputs`` is only the default.

   .. py:attribute:: inputs_from
      :type: str

      The name of a text param whose expression's free names are the block's inputs ("f").

   .. py:attribute:: outputs_from
      :type: str

      The name of a text param of names (``"x v theta"``, space- or comma-separated) that are
      the block's outputs, in order; ``outputs`` is only the default. The first vector input
      gets one component per name, and a ``units`` param gives their units by position.

   .. py:attribute:: elementwise
      :type: bool

      ``True`` for a block written for one number that should also work on vectors: its
      signal ports then take a vector wire too. With a vector of N in, the engine makes N
      instances of the class (each with its own ``self.params`` and state), gives each its
      element (and every number input whole), and gathers each output into a vector of N;
      ``step`` still sees floats. With only numbers in, nothing changes. A block that needs
      the whole vector declares :py:func:`vector_port` ports instead and gets a list.

   .. py:attribute:: cacheable
      :type: bool | None

      ``True`` for a block whose outputs depend on nothing but its params, its inputs and the
      variables it reads through ``ctx.variable()`` (no clock, keys, ``ctx.state``,
      randomness, files or side effects). A backend with ``mode="once"`` then reuses its
      outputs from the last run while none of those changed. Default ``None``: called every
      run.

   .. py:attribute:: feedthrough
      :type: bool

      ``False`` for a block whose outputs this step don't depend on its inputs this step (an
      integrator, a delay). Implement :py:meth:`current`; ``step`` then takes the inputs after
      the pass, and a feedback loop may pass through the block. Default ``True``.

   .. py:method:: current() -> dict

      A block without feedthrough: what it outputs now, from its state alone.

   .. py:attribute:: type
      :type: str

      Its id in the catalog. Required.

   .. py:attribute:: label
      :type: str

      Shown on the block (default: ``type``).

   .. py:attribute:: kind
      :type: str

      ``"source"``, ``"probe"``, ``"function"`` (default) or ``"monitor"``.

   .. py:attribute:: inputs
      :type: list[str | Port]

   .. py:attribute:: outputs
      :type: list[str | Port]

   .. py:attribute:: params
      :type: dict[str, float | Param]

      On the class: the param defaults, in order. On an instance: its values.

   .. py:method:: step(ctx, inputs) -> dict

       Outputs from inputs (dicts by port id). Values are numbers, vectors (``Vector``, a
       list of numbers), tables (lists of rows), text or :py:class:`Handle`. An unconnected
       input is absent. Raise to fail the run; the error names this block.

.. py:class:: Generator(params)

   A signal f(t): implement ``value(t)``. ``t`` is the block's input ``t`` when wired (a
   step's ``progress``), else the run clock; the engine does that and outputs ``y``.

   .. py:method:: value(t) -> float

.. py:class:: Monitor(params)

   A :py:class:`Block` that shows its input on the Dashboard. ``kind = "monitor"``,
   ``inputs = ["in"]``.

   .. py:attribute:: instrument
      :type: str

      ``"graph"`` (default), ``"display"`` or ``"image"``.

   .. py:method:: step(ctx, inputs) -> dict

      Optional: return ``{"value": …}`` to show something other than the input.

.. py:class:: Step(params)

   A block on a Step flow's path. Same definition attributes as :py:class:`Block`;
   ``inputs`` default to ``[Port("in", "step")]``, ``outputs`` to ``[Port("next", "step")]``.

   .. py:method:: enter(ctx, inputs)

      A path arrived.

   .. py:method:: tick(ctx, inputs) -> str

      Each clock step while the path is here: the step output to leave by, or ``""`` to
      stay.

   .. py:method:: may_start() -> bool

      For a hat (step outputs, no step input): whether it may still start a path.

   .. py:method:: ends_run() -> bool

   .. py:method:: current() -> dict

      Its signal outputs now (ports of a type other than ``step``), such as a progress; read
      a clock step later.

.. py:class:: Timed(params)

   A step that does its work over ``seconds`` (a param it must have; default 1). The engine
   follows the optional ``curve`` input (0–1; the first frame stays at 0) when wired, evenly
   otherwise, ends at 1, and outputs ``progress``. Ports: ``in``, ``curve`` → ``next``,
   ``progress``.

   .. py:method:: begin(ctx, inputs)

      A path arrived.

   .. py:method:: advance(ctx, start, end)

      Move the work from progress ``start`` to ``end``.

Station objects
---------------

.. py:class:: StationObject(props)

   A thing on the Station: owns its state and properties (edited on the Station page
   only).

   .. py:attribute:: type
      :type: str

   .. py:attribute:: label
      :type: str

   .. py:attribute:: role
      :type: str

      ``"body"`` (default) or ``"joint"``.

   .. py:attribute:: props
      :type: dict[str, float | Param]

      On the class: the property defaults. On an instance: its values.

   .. py:attribute:: links
      :type: list[str]

      Slots naming other station objects (``["a", "b"]``).

   .. py:attribute:: block
      :type: Linked | None

      Its linked block, if a flow acts on it.

   .. py:method:: read() -> dict

      The linked block's outputs, from the current state.

   .. py:method:: step(ctx, inputs)

      Advance one clock step under the linked block's inputs.

   .. py:method:: entities(id, z) -> list

      Its shapes (``paperboat.scene`` builders, or protocol entity dicts), ids prefixed
      with ``id + "/"``, at depth ``z``.

   .. py:method:: link(slot, other)

      Another station object fills one of its slots, once, before a run (``other`` is
      ``None`` for a toolkit object, which Python can't reach).

Models, solvers and scenes
--------------------------

Bound from the engine's toolkit (C++: :cpp:class:`Model`, ``paperboat::solve``,
``paperboat::scene``), so a domain writes its equations and its look, not its stepping.

.. py:class:: Model(props)

   A :py:class:`StationObject` that is a system of differential equations: it gives its
   initial state and ``derivatives()``, and the engine's solver advances it each clock step.

   .. py:attribute:: solver
      :type: str

      ``"rk4"`` (default), ``"rk45"``, ``"rk2"``, ``"euler"`` or ``"discrete"``. A
      ``solver`` prop (``Param("rk4", "enum", paperboat.solve.names())``) wins, so the user
      can pick one on the Station page.

   .. py:attribute:: x
      :type: list[float]

      The state now, kept current by the engine: use it in ``read()`` and ``entities()``.

   .. py:method:: initial() -> list[float]

      The state when a run starts.

   .. py:method:: derivatives(t, x, inputs) -> list[float]

      ``dx/dt`` at time ``t`` in state ``x``, under the linked block's inputs (a dict;
      unwired ones absent). For ``"discrete"``, the next state.

.. code-block:: python

   class Mass(paperboat.Model):
       type, label = "mass", "Mass on a spring"
       props = {"k": 4.0, "x0": 1.0}
       block = paperboat.Linked(inputs=["force"], outputs=["x", "v"])

       def initial(self):
           return [self.props["x0"], 0.0]

       def derivatives(self, t, x, inputs):
           return [x[1], inputs.get("force", 0.0) - self.props["k"] * x[0]]

       def read(self):
           return {"x": self.x[0], "v": self.x[1]}

       def entities(self, id, z):
           return [paperboat.scene.rect(f"{id}/mass", self.x[0], 0, 0.4, 0.4, "#2f5d62", z=z),
                   paperboat.scene.spring(f"{id}/spring", (-2, 0), (self.x[0] - 0.2, 0))]

.. py:function:: solve.step(solver, f, x, t, dt) -> list[float]

   One step with a fresh solver: ``x`` advanced from ``t`` to ``t + dt`` under
   ``f(t, x)``. ``x`` and what ``f`` returns may be any sequence of numbers (a list, a
   tuple, a numpy array).

.. py:class:: solve.Solver(name="rk4", rtol=1e-6, atol=1e-9, max_steps=100000)

   A solver kept between steps (``rk45`` carries its step size). ``step(f, x, t, dt)``
   returns the new state. An unknown name raises ``ValueError`` naming the solvers.

.. py:function:: solve.names() -> list[str]

.. py:function:: ode(f, x0, solver="rk4", **options) -> ODE

   Bootstrap an ODE system :math:`dx/dt = f(t, x)` with a built-in solver
   (``"rk4"``, ``"rk45"``, ``"rk2"``, ``"euler"``, or ``"discrete"``). Returns an :py:class:`ODE`
   instance whose state is advanced by ``ode.step(dt)`` or ``ode.step(t, dt)``.

.. py:class:: ODE

   .. py:attribute:: x
      Current state vector (list or numpy array, preserving input array type).

   .. py:attribute:: t
      Current simulation time.

   .. py:attribute:: solver
      Name of the active solver algorithm.

   .. py:method:: step(t_or_dt, dt=None)

      Advance the ODE state by one step. Supports calling as ``step(dt)`` or ``step(t, dt)``.

Convenience solver constructors are also available directly on ``paperboat.solve``:
``solve.ode``, ``solve.rk4``, ``solve.rk45``, ``solve.rk2``, ``solve.euler``, and ``solve.discrete``.

The scene builders return :py:class:`Entity` objects; ``entities()`` may return them,
dicts, or lists of them (a spring), mixed. Points are ``(x, y)`` or ``(x, y, z)``.

.. py:function:: scene.rect(id, x, y, width, height, color="#888888", angle=0, z=0)
.. py:function:: scene.circle(id, x, y, radius, color="#888888", z=0)
.. py:function:: scene.box(id, pos, size, color="#888888", rot=None)
.. py:function:: scene.sphere(id, pos, radius, color="#888888")
.. py:function:: scene.line(id, a, b, color="#888888")
.. py:function:: scene.image(id, pos, width, height, src)
.. py:function:: scene.rod(id, a, b, thickness, color="#888888")

   A bar from ``a`` to ``b``.

.. py:function:: scene.spring(id, a, b, coils=8, width=0.2, color="#888888") -> list[Entity]

.. py:class:: Entity

   One shape: ``id``, ``shape``, ``pos``, ``size``, ``rot``, ``color``, ``scale``, ``to``,
   ``src``, each settable.

Domain state
------------

.. py:class:: DomainState

   What a domain's blocks share during a run. The engine makes one per run (from
   :py:attr:`Backend.state`), steps it after each clocked pass and draws it.

   .. py:method:: entities() -> list[dict]

   .. py:method:: patch() -> list[dict]

      The entities that change (default: all).

   .. py:method:: step(ctx) -> str | None

      Advance after a clocked pass; a message ends the run.

   .. py:method:: summary() -> str | None

      Logged when a run ends.

What a block sees
-----------------

.. py:class:: Context

   Passed to every call. Valid during the call only.

   .. py:attribute:: t

      The run clock, s.

   .. py:attribute:: dt

      The clock step, s.

   .. py:attribute:: keys

      The set of keys held down in the UI (``KeyboardEvent.code`` values).

   .. py:attribute:: state

      The domain state for this run, or ``None``.

   .. py:attribute:: node

      The id of the block (or station object) being called.

   .. py:method:: variable(name)

      A project variable's value now: a number, a boolean as 1.0 or 0.0, text; ``None`` if
      there is none of that name.

   .. py:method:: set_variable(name, value)

      Set a ``tunable`` variable (#166), as an autotuner or a calibration does: a number
      (or ``bool``) for a number or boolean variable, text for an enum, text or file one.
      It takes effect at the end of the pass; formulas and params bound to it follow, and
      the UI keeps the value in the project. Returns ``False``, with a warning in the
      Console, if refused (unknown, not tunable, a formula, the wrong type, out of range).

   .. py:method:: log(text, level="info")

      Write a line to the Console (``info``, ``warn``, ``error``), tagged with this block.

   .. py:method:: progress(fraction)

      How far a long step has got, 0 to 1: the UI shows it on the block.

.. py:class:: Handle

   Opaque data on a wire (an image…): pass it along as is. ``kind`` names what it holds.

Serving
-------

.. py:class:: Backend

   .. py:attribute:: engine
      :type: str

      Its name in ``hello``.

   .. py:attribute:: descriptor
      :type: pathlib.Path

   .. py:attribute:: blocks

      A list of classes: the domain's :py:class:`Block`, :py:class:`Monitor` and :py:class:`Step` classes.

   .. py:attribute:: objects

      A list of classes: the domain's :py:class:`StationObject` classes.

   .. py:attribute:: state
      :type: Callable[[], DomainState] | None

      A fresh domain state per run.

   .. py:attribute:: mode
      :type: str

      ``"clock"`` (default) or ``"once"``.

   .. py:attribute:: dt
      :type: float

   .. py:attribute:: duration
      :type: float

   .. py:attribute:: emit_every
      :type: int

   .. py:attribute:: speed
      :type: float

      Times real time (0: as fast as it can). The defaults here are what the UI shows; a user
      may change ``duration`` and ``speed`` per run.

   .. py:attribute:: view
      :type: dict | None

      The scene's camera hint.

   .. py:attribute:: port
      :type: int

.. py:function:: serve(backend)

   The engine's command line: serve on ``--port N`` until Ctrl+C, or with ``--run`` run the
   starter once headless, print each protocol message as a JSON line and exit (0 when
   done).
