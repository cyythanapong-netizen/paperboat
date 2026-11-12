User Manual
===========

Paperboat is an interactive cockpit for engineering simulations, signal processing,
control systems, and data pipelines. It pairs a high-performance C++ engine with a clean,
responsive interface that adapts to whatever domain your backend provides.

The look and feel follows a clear design philosophy: **flat mono, like a well-made instrument**.
Surfaces are grey panels with hairlines, input fields are recessed grey wells, output
readouts sit in crisp black displays, and a single signal colour means "live" or "look here".

This manual covers everything you need to navigate, build, run, and monitor systems in
Paperboat.

Overview of the Workspace
-------------------------

The workspace is organized around four core pages and an overarching control bar:

1. **Station**: The setup and equipment page. It displays a 2D or 3D scene of the physical
   things being simulated or controlled (masses, motors, joints, cameras), along with their
   parameters and initial states.
2. **Flow**: The procedure page. An intuitive canvas where computational and signal blocks
   are wired together into graphs.
3. **Board**: The live operations deck (referred to as the Dashboard in technical specs).
   Instruments—such as real-time wave plots, numeric cards, and image feeds—show what the
   system is doing as it runs.
4. **Console**: A streaming diagnostic log that records events, warnings, block-attributed
   messages, and run completion summaries.

.. code-block:: text

   ┌──────────────────────────────────────────────────────────────────────────┐
   │ ⛵ paperboat  File  Edit  View  Run            [ navigator ]  commander  ● engine │
   ├─────────┬────────────────────────────────────────────────────────────────┤
   │   ⬡     │ Flow                                                 6 blocks  │
   │ station │                                                                │
   │ ┌  ⌥  ┐ │        ┌ PID ───── fn ┐     ┌ Sum ───── fn ┐                   │
   │ │ flow│ │        │ kp      40   │ ─── │ a            │                   │
   │ └     ┘ │                                                                │
   │   ▦     │      ┌ ▶ Run  ■ Cancel  for ░10░ s ░1×░  running 3.2 / 10 s ┐    │
   │  board  │                                                                │
   └─────────┴────────────────────────────────────────────────────────────────┘

View Modes
----------

Depending on whether you are authoring a system, tuning parameters, or running an active
test, Paperboat offers three specialized **View Modes**:

.. list-table::
   :header-rows: 1
   :widths: 18 42 40

   * - Mode
     - What It Shows
     - Best For
   * - **Navigator** (default)
     - Shows one page at a time at full size. The page rail sits on the left, sliding smoothly
       between Station, Flow, and Board.
     - Focused work: wiring a complex flow, fine-tuning station geometry, or inspecting a log.
   * - **Commander**
     - Places the **Station** and **Flow** side-by-side in split panels, with the **Board**
       available as a pull-up drawer at the bottom.
     - Authoring and tuning: editing physical parameters or block gains while watching live
       oscilloscope responses in the drawer.
   * - **Lookout**
     - The **Board** fills the screen. The Station and Flow fold into collapsible side drawers.
     - End-user monitoring, live test stands, and production rigs where telemetry is primary.

Switching Modes
~~~~~~~~~~~~~~~

- Click the mode name on the top navigation bar (`[ navigator ]  commander  lookout`). Selected
  options are highlighted with bold ink in brackets, never a filled shape.
- Press :kbd:`Alt` + :kbd:`M` to cycle through the three modes sequentially.
- In Navigator mode, press :kbd:`Alt` + :kbd:`1` (Station), :kbd:`Alt` + :kbd:`2` (Flow), or
  :kbd:`Alt` + :kbd:`3` (Board) to jump directly to a page.

Working with the Station
------------------------

The Station page represents the "physical" world of your project. **View > Station page**
shows or hides it; a hidden Station leaves the other pages as they are (Commander puts the
Flow and the Board side by side, and Lookout has no Station drawer). A backend can start with
it hidden by setting ``[station] visible = false`` in its descriptor.

Interacting with the 3D Scene
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When your backend provides a 3D station (such as rigid bodies, vehicles, or kinematic arms):

- **Orbit / Rotate**: Click and drag with the primary (left) mouse button.
- **Pan**: Drag with the secondary (right) mouse button, or hold :kbd:`Shift` while dragging
  with the left mouse button.
- **Zoom**: Rotate the mouse scroll wheel.

Editing Station Objects
~~~~~~~~~~~~~~~~~~~~~~~

1. Click an object directly in the 3D scene or select it from the sidebar list.
2. The property panel displays its properties (mass, damping, stiffness, initial position,
   dimensions).
3. Type updated values directly into the recessed grey input fields.
4. Linked blocks on the Flow page update their behavior on the next run.

.. note::

   Station properties are edited exclusively on the Station page. The Flow page reads from
   and acts upon station objects via their **linked blocks**, but wires never overwrite
   station properties directly.

Working with the Flow Canvas
----------------------------

The Flow page is where you construct the logic, signal generation, feedback loops, and data
pipelines of your application.

Adding Blocks
~~~~~~~~~~~~~

There are three ways to add blocks:

- **Quick Add**: Press :kbd:`/` or double-click anywhere on empty canvas space to open a
  fast search palette at your cursor. Type the name of a block (e.g., `sine`, `pid`, `gain`,
  `monitor`) and press :kbd:`Enter`.
- **Block Browser**: Click **+ Add** on the canvas header to browse cards organized by
  package (`signal`, `math`, `step`, `dashboard`, domain-specific packages). Drag cards
  onto the canvas.
- **Insert onto Wire**: Drag a block card from the browser directly over an existing wire.
  The wire will split and reconnect through the inserted block.

Wiring Ports
~~~~~~~~~~~~

- Every block has input ports on its left and output ports on its right.
- Click and drag from an output port to an input port. A connecting wire will snap into place.
- Wires connect compatible types:
  - **Signal wires** (thin lines): carry numeric continuous or discrete values.
  - **Step wires** (dark lines with directional arrows): carry execution control tokens in Step flows.
  - **Vector wires**: carry multi-channel arrays (such as 3D coordinates or RGB vectors).
  - **Image wires**: carry raster frames and visual buffers.
- An unwired signal input automatically defaults to `0.0`.
- Wires run square (horizontal and vertical runs only). Select a wire and drag the handle on any
  of its segments to move that segment; the bends are saved with the project.

Mixing Step and Signal Flow
~~~~~~~~~~~~~~~~~~~~~~~~~~~

A sheet can hold both layers; it is still one canvas. Signal blocks outside stories run every
clock tick, in dependency order. Step paths advance from their hats (`Start`, `When`, or
`Signal → Step`) and stop at `Wait`, `Join` or a condition. Add a step block to a Signal sheet
and the sheet becomes a mixed flow; the Add browser offers both layers.

A **story** is a state: its internal signal blocks run only while a path is at it. `reset`
starts its blocks afresh on each entry; without it the state carries over. The `Signal → Step`
block turns a 0/1 (boolean) signal into a step wire, starting a path when it rises (or falls).

Configuring Block Parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Click a block to focus it. Parameters appear as recessed wells directly on the block face or
in the inspector:

- **Numeric fields**: Type values or use arrow keys to nudge.
- **Variable fields**: A parameter that names a project variable (the **Variable** block's
  ``name``, or the target of a tuner block) is a picker: type a name, choose one from the
  list, or drop a variable from the Variables drawer on it. A block that sets that variable
  during a run (an autotuner, a calibration) updates it live, and the new value stays in the
  project when the run ends.
- **Live variables from Squero Studio MX**: while MX runs next to Paperboat, its workspace
  shows in the Variables drawer with an **MX** badge (type ``number``, ``text`` or
  ``matrix``; hover a row for its full value). They are read-only and never saved in the
  project. Drop one on a field to bind it: a ``matrix`` fits a text field such as the
  **Matrix** block's ``values``. A run sends the value MX last broadcast in place of the
  name, so the binding follows MX between runs. A matrix of more than 4096 values is not
  shared live: its row says so, and a run that uses it stops with that reason.
- **Function Expressions** (:code:`f(t)`): In expression blocks (like `signal/function`), you
  can type arbitrary formulas such as:

  .. code-block:: text

     sin(2 * pi * f * t) + amp * cos(omega * t)

  Paperboat automatically parses the formula and **dynamically generates input ports** on
  the block for every variable name found (`f`, `amp`, `omega`). The time variable `t`
  is special: when connected, it reads your custom signal; when unwired, it automatically
  reads the simulation clock time!

Grouping and Subsystems
~~~~~~~~~~~~~~~~~~~~~~~

- **Visual Groups**: Select multiple blocks and press :kbd:`Ctrl` + :kbd:`G` to enclose them
  in a titled bounding box. This keeps complex diagrams readable without altering execution.
- **Subsystems**: Collapse a group into a single compact block with clean external ports.
  Double-click a subsystem block to navigate into its internal sheet. Use the breadcrumb bar
  at the top or press :kbd:`Escape` (or :kbd:`Alt` + :kbd:`↑`) to pop back up.

Commenting and Annotating Blocks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

During iterative testing, tuning, and debugging, you can temporarily disable or bypass blocks
without deleting them, losing parameters, or rewiring ports:

- **Comment Out** (:kbd:`Ctrl` + :kbd:`/` or :menuselection:`Edit --> Comment out`):
  Select one or more blocks and press :kbd:`Ctrl` + :kbd:`/` (or click **Comment** on the
  floating selection toolbar). The blocks dim to muted opacity with a ``// commented`` tag,
  and connected wires dim. Commented-out blocks are excluded from simulation; downstream blocks
  cleanly receive default unwired values (:code:`0.0`). Feedback loops through commented-out
  blocks do not produce algebraic loop cycle errors.
- **Comment Through / Bypass** (:kbd:`Ctrl` + :kbd:`Shift` + :kbd:`Y` or click **Bypass**):
  Bypasses the block's internal computation and passes signals directly from input ports
  to matching output ports (e.g. single-input single-output blocks like PID controllers,
  gains, delays, or filters pass ``in`` directly to ``out``). In Step flows, execution tokens
  immediately traverse across bypassed step blocks to their ``next`` output.
- **Block Notes & Documentation**:
  Click the small **note** button in any block header to open a recessed well where you can
  record formulas, design notes, or tuning rationale. The notes are saved directly in your
  :code:`.ppj` project file.
- **Canvas Annotations** (:code:`base/comment`):
  Add a freeform note card from the Block Palette (:code:`base/comment`) onto the canvas to
  document subsystems, test setups, or architecture directly alongside your blocks.

The Board (Instruments & Telemetry)
-----------------------------------

The Board displays live telemetry produced by **Monitor** blocks in your flow.

Available Instrument Types
~~~~~~~~~~~~~~~~~~~~~~~~~~

- **Wave Plots** (:code:`graph`): Live scrolling oscilloscopes displaying signals over time.
  Plots feature automatic vertical auto-scaling, clear grid hairlines, and legend labels.
- **Numeric Displays** (:code:`display`): High-legibility digital readouts in black display wells,
  ideal for setpoints, current velocities, errors, or cycle counts.
- **Image Displays** (:code:`image`): Real-time visual streams, camera feeds, or generated plots.
- **Scope** (:code:`scope`): The instrument to study signals with: several channels (a vector
  wire is one channel per component), an axis per unit, zoom and pan, cursors, a trigger, xy
  and fft modes, CSV and PNG export. While a Scope is on the Board the run takes its
  smallest solver step as the sampling step, so the plot and exports show those points.
  Drag to pan (this freezes it), :kbd:`Ctrl` + wheel to
  zoom time, :kbd:`Shift` + :kbd:`Ctrl` + wheel to zoom values, :kbd:`Ctrl`-drag to zoom to a
  box, double-click to autoscale and follow the run again. With the Scope focused:
  :kbd:`F` freeze or follow, :kbd:`C` cursors, :kbd:`T` trigger, :kbd:`A` autoscale.
  **+ tap** wires any output into it. Its mode, window, trigger, draw style and hidden
  channels save with the project.

Arranging the Board
~~~~~~~~~~~~~~~~~~~

Paperboat automatically positions instruments based on the order and position of monitor
blocks on your Flow canvas. You can customize the arrangement at any time:

- Click **Arrange** to enter layout editing mode.
- Change tile sizes: **Compact** (1×1), **Normal** (2×1), **Large** (2×2), or **XLarge** (3×3).
- Drag tiles into your preferred order across the responsive grid.
- Add **Sections** (bordered groups with headers) or **Separators** to partition subsystems.
- Embed live **Variables** directly into section titles and note cards using brackets, such
  as :code:`Current Gain: {kp}`.

Running and Monitoring
----------------------

The Run Dock
~~~~~~~~~~~~

The Run Dock floats gracefully at the bottom of the canvas, rendered in semi-transparent glass:

.. code-block:: text

   ┌ ▶ Run  ■ Cancel  for ░10░ s ░1×░  running 3.2 / 10 s ┐

- **Run / Pause**: Click :kbd:`▶ Run` or press :kbd:`Space`.
- **Cancel**: Click :kbd:`■ Cancel` to stop the current run immediately.
- **Duration**: Edit the run duration in seconds (e.g., `10 s`). Set to `0` or leave empty
  for continuous runs if supported by the backend.
- **Speed Multiplier**: Toggle execution pacing between `0.5×`, `1×`, `2×`, or uncapped `Max`.
- **Solver**: the Run menu picks the run's solver (`RK4`, `RK45`, `RK2`, `Euler`,
  `Discrete`, and the implicit `CVODE` and `IDA` for stiff systems: chemistry, circuits,
  fast and slow dynamics together); **Run > Solver settings…** sets its options: tolerances
  and the smallest and largest internal solver step for `RK45`, `CVODE` and `IDA`,
  sub-steps per clock step for the fixed-step solvers, and the **clock step**. The clock
  step is how often blocks run and monitors sample. A Scope is an instrument, not a
  glance: while one is on the Board, the run uses the smallest solver step as its clock
  (the max step, or the clock step divided by the fixed sub-steps) and sends the Scope a
  sample on every pass, so the plot and its CSV/PNG export follow the solver's step, not the
  coarse clock. The
  Graph still draws one frame every few steps. Every
  block and station object whose own `solver` is `auto` (the default) uses it; the settings
  save with the project.

Replay and Stepping
~~~~~~~~~~~~~~~~~~~

A finished interactive run keeps its samples and Station motion, so it can be studied again
without rerunning it. Click **⏱ Replay** on the dock (or **Run > Replay last run**):

- **Timeline**: drag the scrubber to jump to any sim time.
- **Step back / forward** (:kbd:`◀` / :kbd:`▶`): move one sample interval, the run's own
  :math:`\Delta t`, so a mode switch or an edge can be inspected.
- **Play / pause** (:kbd:`▶` / :kbd:`❚❚`): play the run again; at the end it starts over.
- **Speed**: `0.25×`, `0.5×`, `1×`, `2×` or `5×` wall-clock playback.
- **Live**: leave replay and follow the next run.

While a replay is open, every Scope shows a needle at the playhead, Board readouts show the
values at that time, wires into monitors carry the value they had then, and the Station shows
the entity positions at that time (a web Station gets the same scene updates). Replays read
the in-memory trace store, and a saved **run record** can be opened with
**File > Open run record…**; **File > Save run record…** writes the current run's samples and
Station history as JSON (:code:`record.json` from :code:`paperboat run --record`, #277).

The Console
~~~~~~~~~~~

Click the **Console** tab on the rail or open it from the Board drawer to inspect run logs:

- Output lines are categorized as `info`, `warn`, or `error`.
- When an error occurs, the Console prints the exact node ID that failed. Clicking the node
  link in the Console immediately highlights and pans to the offending block on the Flow canvas.

The Workbench (no domain)
~~~~~~~~~~~~~~~~~~~~~~~~~

Paperboat's engine includes a **workbench** backend with only the standard toolkits
(`base`, `math`, `signal` and `dashboard`), no domain, and every optional toolkit
installed in the checkout (`control`, `opencv`, `optimization`…). It listens on the
default backend port, so the web app connects to it as it is:

.. code-block:: sh

   ./bin/paperboat start         # web app on :3047, workbench on ws://localhost:8765
   npm run backend:workbench     # or just the backend
   npm run dev                   # or just the web app, http://localhost:3047

The Flow opens with a Sine into a Graph; build any signal, mathematical or control model
from the palette and run it. The Station page is hidden by default (View > Station shows
it): a station belongs to a domain backend, and the workbench has none. The optional
blocks are in the palette once their toolkit is installed in ``engine/toolkit``.
Connect a domain backend from **File > Backend…** to switch.

Project Management
------------------

Paperboat saves your entire workspace into human-readable, version-controlled project files
with the extension :code:`.ppj` (JSON inside).

- **Saving**: Press :kbd:`Ctrl` + :kbd:`S` (or :kbd:`File > Save`) to save changes. To save a copy,
  choose :kbd:`File > Save As...`.
- **Opening**: Press :kbd:`Ctrl` + :kbd:`O` (or :kbd:`File > Open`), or drag-and-drop any
  :code:`.ppj` file (or an older :code:`.paperboat.json`) directly into your browser window.
- **Session Restoration**: If your browser accidentally closes or refreshes during a run,
  reopening the app presents an option to restore your unsaved session state.

Squero Studio: OpenModelica
---------------------------

Squero Studio OM is a separate app, in the ``squero-studio-om`` repository, for building the equipment a Paperboat flow runs: you draw
a Modelica model, and Paperboat opens the backend generated from it. It is not a page of the
app above. Start it with ``paperboat start squero-studio-om [Model.mo]``, which fetches and builds the app from GitHub the first time
(``paperboat pkg`` manages it: ``pkg versions squero-studio-om``, ``pkg update squero-studio-om``), and open ``http://localhost:59201``; with no file it opens ``./Untitled.mo``, creating it if it isn't there. It needs
OpenModelica (``omc``) on the path.

The bar reads ``squero studio [ OM ]``, then the menus (Edit, View), the model's name and, at
the right, the compiler's state: ``omc 1.27.0 · ready``, ``checking`` while it works, ``failed``
when it has stopped. Studio has no modal dialogs; a refusal is a sentence at the foot of the canvas.

**The library** (left) lists the Modelica Standard Library one package at a time (click a package
to open it) and, under *This project*, other classes the server has loaded. Each part shows its
icon. Drag a part onto the canvas, or double-click it, to place it. The filter box searches every
part by name, then by path.

**The canvas** is the model's diagram. Every edit is a call to omc (``addComponent``,
``addConnection``, ``setComponentModifierValue``…) followed by reading the model back, and the file
is saved after each one, so the ``.mo`` stays a normal Modelica file that OMEdit opens.

- *Wiring*: drag from a port to another. A physical (acausal) wire has no direction and may start at
  either end; a signal (causal) wire runs from an output to an input and is drawn with an arrow.
  A wire the rules refuse is not drawn: the reason is written at the foot of the canvas
  (``a PositivePin does not join a Flange_a``, ``an input does not join an input``). Three or more
  wires on a port draw a junction dot. A wire's stored bends (``Line(points)``) are kept on the 20px grid. Wires run square, as on the
  Flow canvas: select one and drag a segment's handle to move it; the new bends are written to the ``.mo``.
- *Moving and deleting*: drag a part to move it; :kbd:`Delete` removes the selected parts or wire.
- *Quick add*: press :kbd:`/`, type a part name and press :kbd:`Enter` to place it. With a wire
  selected the part is *spliced in*: the line above the list says exactly what will happen,
  for example ``Insert R2: R1.n -> p, n -> L1.p``, and :kbd:`Enter` does it as one step.
- *Undo*: :kbd:`Ctrl` + :kbd:`Z` and :kbd:`Ctrl` + :kbd:`Shift` + :kbd:`Z` step through your edits; a splice is one step.
- A part with an ``if`` connector shows it only while its parameter is on; an array connector is
  one port with a count.

**The split view** (right) shows the model beside the canvas. ``[ .mo ]`` is the Modelica text
(read-only for now). ``[ causal ODE c++ ]`` is the backend Studio would generate, from a dry run:
nothing is written. Above the code it says which way the model goes, ``ode`` (a plain C++ model)
or ``fmu`` (omc's compiled model behind an adapter), and why; an FMU model shows no code, since
omc builds it. The dock's *Export backend* writes the real thing.

**Simulate** is a check, not a second Flow page: set a stop time, press *Simulate*, and omc runs
the model and plots chosen variables. Switch variables on and off with the chips under the plot.
Nothing leaves Studio: no block, board or backend is involved; put the model in a flow by exporting it.

Keyboard Shortcuts
------------------

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Shortcut
     - Action
   * - :kbd:`Space`
     - Start or pause simulation run
   * - :kbd:`Ctrl` + :kbd:`K` (or :kbd:`Cmd` + :kbd:`K`)
     - Open Command Search palette
   * - :kbd:`/`
     - Quick add block at cursor (on Flow canvas)
   * - :kbd:`?`
     - Show canvas gestures and shortcut cheat sheet (on Flow canvas)
   * - :kbd:`/` in Squero Studio
     - Add a part to the Modelica diagram, or splice it into the selected wire
   * - :kbd:`Alt` + :kbd:`V` in Squero Studio
     - Open or close the interface sheet
   * - :kbd:`Ctrl` + :kbd:`/`
     - Comment out / uncomment selected blocks
   * - :kbd:`Ctrl` + :kbd:`Shift` + :kbd:`Y`
     - Comment through / bypass selected blocks
   * - :kbd:`Alt` + :kbd:`M`
     - Cycle View Modes (Navigator → Commander → Lookout)
   * - :kbd:`Alt` + :kbd:`1`
     - Switch to Station page
   * - :kbd:`Alt` + :kbd:`2`
     - Switch to Flow page
   * - :kbd:`Alt` + :kbd:`3`
     - Switch to Board page
   * - :kbd:`Alt` + :kbd:`↑` or :kbd:`Esc`
     - Exit subsystem / navigate up one level
   * - :kbd:`Ctrl` + :kbd:`S`
     - Save project
   * - :kbd:`Ctrl` + :kbd:`O`
     - Open project file
   * - :kbd:`Ctrl` + :kbd:`Z` / :kbd:`Ctrl` + :kbd:`Shift` + :kbd:`Z`
     - Undo / Redo canvas modifications
   * - :kbd:`Delete` or :kbd:`Backspace`
     - Delete selected blocks or wires
