Python API
==========

.. py:module:: paperboat

``paperboat`` binds the C++ engine (``paperboat._engine``, nanobind). The engine does
everything a backend does; Python provides only the domain's own code, as subclasses of
the classes below. **A class's attributes define it** (what the UI shows), **its methods
are what the engine calls**. The classes a backend passes become the domain's package
(named after the descriptor's ``domain``), so the palette has them without the descriptor
listing them.

Install: ``pip install ./python`` (or ``npm run build:python`` and ``PYTHONPATH=python`` while
developing). Never implement a framework feature here: add it to the engine and bind it.

Definitions
-----------

.. py:class:: Port(id, type="signal")

   A port with a type other than ``"signal"``: ``Port("points", "points")``,
   ``Port("in", "step")``. Plain strings are signal ports.

.. py:class:: Param(default, type="number", options=())

   A param (or property) whose type its default doesn't say: ``Param("Space", "key")``,
   ``Param("data/a.png", "file")``, ``Param("fast", "enum", ["slow", "fast"])`` (a dropdown:
   the block gets the name). Plain defaults say it: a number (``number``), a bool
   (``boolean``, a checkbox, 1.0 or 0.0 to the block), text (``text``).

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
      ``compact`` (a small plate with bare port dots and a caption), ``glyph`` (a curve name:
      ``gain``, ``plus``, ``step``, ``clamp``, ``const``; or a few characters drawn as text)
      and ``value`` (an expression of the params for the caption, such as ``"{k}"``).

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

      Outputs from inputs (dicts by port id). Values are numbers, lists, tables (lists of
      rows), text or :py:class:`Handle`. An unconnected input is absent. Raise to fail the
      run; the error names this block.

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

   .. py:method:: entities(id, z) -> list[dict]

      Its shapes (protocol entities), ids prefixed with ``id + "/"``, at depth ``z``.

   .. py:method:: link(slot, other)

      Another station object fills one of its slots, once, before a run (``other`` is
      ``None`` for a toolkit object, which Python can't reach).

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
