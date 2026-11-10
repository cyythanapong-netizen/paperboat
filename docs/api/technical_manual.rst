Technical Manual
================

This technical manual details the underlying architecture, execution semantics, protocol
mechanics, and extensibility model of the Paperboat framework.

While the :doc:`user_manual` addresses the day-to-day operation of the web interface, this
document serves developers, systems engineers, and contributors implementing backends,
embedding the engine, or authoring custom toolkits.

Architecture Overview
---------------------

Paperboat is architected as four decoupled layers:

.. code-block:: text

   ┌─────────────────────────────────────────────────────────────┐
   │ React Web Client (@paperboat/web)                           │
   │ Zero-domain UI, dynamic catalog rendering, WebGL 3D scene   │
   └──────────────────────────────┬──────────────────────────────┘
                                  │ WebSocket (paperboat/0)
   ┌──────────────────────────────▼──────────────────────────────┐
   │ C++ Core Engine (paperboat::core)                           │
   │ Topological scheduler, session manager, HTTP/WS server      │
   ├──────────────────────────────┬──────────────────────────────┤
   │ C++ Toolkit (paperboat::toolkit)  │ Python Bindings (pybind11)  │
   │ Standard blocks, math, generators │ paperboat._engine extension │
   └──────────────────────────────┴──────────────────────────────┘

1. **C++ Core Engine (`paperboat::core`)**: High-performance, portable C++20 engine that
   manages graph compilation, cycle detection, topological execution scheduling, run clock
   stepping, and network sessions. It owns no domain physics or visual rendering logic.
2. **Standard Toolkit (`paperboat::toolkit`)**: Modular library of reusable block primitives
   (`base`, `signal`, `math`, `dashboard`, `step`), plus optional external toolkits such as
   `control` (`paperboat-toolkit-control`).
3. **Python Bindings (`paperboat._engine`)**: Native C-extension bindings exposing the C++
   scheduler and data types to Python backends with zero protocol serialization overhead.
4. **Web Frontend (`apps/web`)**: A domain-agnostic React/TypeScript interface that builds its
   entire UI dynamically from the backend's declared catalog and starter definitions.

Fault Containment Principle
~~~~~~~~~~~~~~~~~~~~~~~~~~~

A fundamental guarantee of the Paperboat engine is that **a user's graph or block error
never crashes the backend process**.

- Divide-by-zero, out-of-bounds array access, or uncaught exceptions thrown during a block's
  `step()` call are trapped by the execution harness.
- The engine isolates the error, tags the offending node ID, emits a `node.state = "failed"`
  event, logs an attributed error message, and transitions the run to status `failed`.
- The backend remains online, accepting subsequent graph edits from the client.

The WebSocket Protocol (`paperboat/0`)
--------------------------------------

Communication between the web UI and the backend is multiplexed over a single HTTP/WebSocket
connection. Every message is a structured JSON payload tagged with a `type` string. The
authoritative schema is specified in :file:`protocol/schema/paperboat-0.schema.json`.

Connection Handshake
~~~~~~~~~~~~~~~~~~~~

Upon establishing the WebSocket connection, the backend initializes the client state in a
strict, ordered sequence:

.. code-block:: text

   Client                                            Backend
     │                                                  │
     │ ────────────── WebSocket Connect ───────────────▶│
     │                                                  │
     │ ◀───────────── hello ────────────────────────────│ (engine name, protocol version, view mode)
     │ ◀───────────── catalog ──────────────────────────│ (available block & object definitions)
     │ ◀───────────── graph.load ───────────────────────│ (starter graph topology)
     │ ◀───────────── station.load ─────────────────────│ (starter station objects)
     │ ◀───────────── scene ────────────────────────────│ (initial 3D/2D visual entities)
     │                                                  │

Execution Lifecycle
~~~~~~~~~~~~~~~~~~~

When the user initiates a run, the client serializes the graph and active station into a
`graph.run` payload:

.. code-block:: text

   Client                                            Backend
     │                                                  │
     │ ────────────── graph.run ───────────────────────▶│ (nodes, edges, groups, station overrides)
     │ ◀───────────── run.started ──────────────────────│ (acknowledgement with run ID)
     │                                                  │
     │ ◀─ stream ─── signal (monitor telemetry) ────────│ (emitted per clock tick)
     │ ◀─ stream ─── scene.patch (entity transforms) ───│ (emitted per clock tick)
     │ ◀─ stream ─── node.state (running / done) ───────│ (step flow transitions)
     │ ◀─ stream ─── run.progress (fraction 0..1) ──────│ (periodic progress update)
     │                                                  │
     │ ◀───────────── run.finished ─────────────────────│ (status: "done" | "failed" | "cancelled")
     │                                                  │

Interactive Control Messages
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

While a run is active, the client can dispatch real-time events:

- `run.cancel`: Immediately signals the scheduler to abort execution.
- `run.speed`: Changes the run's speed (`{ speed: 4 }`; `0` is as fast as it can). Pacing
  re-anchors at the moment of the change, so the run goes on from where it is at the new
  speed, and the backend answers with `run.speed` (`run`, `speed`) once it took it.
- `input`: Forwards raw keyboard events (`{ key: "ArrowUp", down: true }`) to any active
  `Key` blocks in the flow.
- `station.set`: Transmits edited station properties from the Station panel. The backend
  recomputes geometries and responds with an updated `scene` payload.

Differential Scene Updates (`scene.patch`)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To sustain 60 FPS visual animation across complex 3D scenes without saturation of WebSocket
bandwidth, Paperboat employs differential entity updates:

- The initial `scene` message sends complete entity definitions (geometry type, dimensions,
  color, baseline position, rotation).
- Each subsequent simulation tick emits `scene.patch`, which carries **only modified fields**
  keyed by entity ID:

  .. code-block:: json

     {
       "type": "scene.patch",
       "entities": {
         "pendulum-bob": { "pos": [0.85, 1.42, 0.0], "rot": [0.0, 0.0, 0.73] },
         "spring-wire": { "to": [0.85, 1.42, 0.0] }
       }
     }

Engine Execution Semantics
--------------------------

Graph Compilation & Topological Ordering
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When `graph.run` arrives, `paperboat::core::prepare_graph` validates the topology before any
block is instantiated:

1. **Catalog Verification**: Every node's `type` is resolved against the loaded `Catalog`.
   Missing types are retained in client memory as "missing" nodes, but will fail validation.
2. **Type Compatibility**: Port types must match across wires:
   - `signal` → `signal`
   - `step` → `step`
   - `vector` → `vector` (with matching component lengths)
   - `vector` → an elementwise block's `signal` port (marked `broadcast`)
   - `numbers` (a list of numbers, #24) → `numbers`, `vector` or a `broadcast` port, one unit
   - `points` (a list of points, #24) → `points`, coordinates per point and their units matching
3. **Cycle Detection**: Direct feedthrough cycles (algebraic loops) in Signal flows are
   prohibited. A loop is permitted only if it is broken by a state-holding block (such as an
   explicit `Delay` block, a `Step` flow path boundary, or a Station object's linked block
   which evaluates in two decoupled stages: `read()` before the pass and `step()` after).
4. **Vector Lanes**: An elementwise block (`BlockDef::elementwise`) fed a vector, straight or
   through other elementwise blocks, runs as N lanes: N instances, each given its element of
   every vector input and every number input whole, their outputs gathered into vectors of N.
   N is resolved from the wires alone, iterating to a fixed point (a block's length only grows
   from "numbers" to a vector of N), so an Integrator in a vector loop knows N before its first
   step. Two vectors of different lengths into one block fail here, naming the block and port;
   a length known only at run time into a block without feedthrough fails here too. The block's
   outputs then become vector ports of N, so a wire from one into a plain `signal` port is
   refused, and a wire into a vector port of another length is too.
5. **Topological Sort**: The execution sequence is determined via Kahn's algorithm, producing
   an execution order where every block's inputs are fully resolved before its `step()` method
   is called.

Dynamic Port Derivation (`inputs_from` & `ports_from`)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Certain blocks determine their ports dynamically based on text configuration rather than
static declarations:

- `ports_from = "signs"`: Used by algebraic summing junctions (`math/sum`). Each character
  in a sign string (e.g., `"+-+ "` generates ports `a`, `b`, `c` with their respective signs).
- `ports_from = "names"` and `outputs_from = "names"`: Mux's inputs and Demux's outputs, one
  per name in a space- or comma-separated list (`"x v theta"`), units by position from a
  `units` param; the vector port on the other side gets one component per name. Bad names fail
  the run on the block. The UI derives the same ports (`ports.ts`); both are checked against
  one fixture (`engine/tests/data/ports.json`).
- `inputs_from = "f"`: Used by free expression function blocks (`signal/function`). The engine
  invokes `Expression::free_names(text)` to scan identifier tokens in appearance order:

  - Scientific notation exponents (such as `1.5e-3`) are recognized and skipped.
  - Built-in constants (`pi`, `e`) and mathematical functions (`sin`, `cos`, `clamp`) are
    distinguished from variables.
  - The variable `t` is recognized as the simulation clock. If `t` is wired, it reads the
    external signal; if left unwired, it automatically binds to `Context::t`.
  - All other free variable identifiers become typed input ports defaulting to `0.0` when
    unwired.

Run Styles
~~~~~~~~~~

Paperboat supports three distinct execution models:

1. **Clocked Signal Flow (`ClockRun`)**:
   Fixed time step `dt` across duration `T`. Discrete-time update loop per tick:

   - Increment simulation clock: :math:`t \leftarrow t + dt`.
   - Evaluate signal blocks in topological dependency order.
   - For each station object, invoke `step(ctx, in)` using inputs gathered from linked blocks.
   - Evaluate domain-level state `DomainState::step(ctx)`.
   - Sample Monitor blocks and dispatch `signal` packets.
   - Compute scene differential changes and dispatch `scene.patch`.

2. **Step Flow (`StepRun`)**:
   Token-passing discrete event execution.

   - Hat blocks (like `Start` or `When`) initiate concurrent execution paths.
   - Blocks implement `enter(ctx)` and `tick(ctx, in)`. When a block completes its task, it
     indicates which outgoing step wire the token should traverse next.
   - Story groups act as atomic procedural steps, executing their internal signal subgraphs
     for a prescribed duration before passing control to the subsequent step.

3. **Once Flow (`OnceRun`)**:
   Non-clocked, single-pass batch execution.

   - Suitable for CAD model generation, build automation, dataset extraction, or image
     processing pipelines.

The Descriptor System (`descriptor.toml`)
-----------------------------------------

The descriptor file defines the metadata, palette offerings, and starter configurations
for a backend app. It is strictly declarative: **it never defines block implementations**,
which reside exclusively in C++ or Python code.

Schema Breakdown
~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Field
     - Description
   * - `domain`
     - Domain namespace string (e.g., `"robotics"`). Blocks registered under this package
       are imported automatically into the palette.
   * - `import`
     - Array of toolkit packages to load into the catalog (e.g., `["base", "signal", "math", "dashboard"]`).
   * - `[[blocks]]`
     - Customizes or specializes a block template. May override `label`, `defaults`, or
       supply a custom `config` expression to specialize an extensible parent (such as `signal/generator`).
   * - `[[objects]]`
     - Declares station object templates, property defaults, and linked block associations.
   * - `[graph]`
     - Defines the initial starter flow topology (`nodes`, `edges`, `groups`).
   * - `[station]`
     - Defines initial station objects, scene camera view presets (`2d`, `3d`, or `web`),
       coordinate extents, and whether the Station page is shown by default (`visible`).
   * - `[dashboard]`
     - Declares the default layout of instrument tiles, section groupings, and formatted
       label cards.

Extending Parent Blocks in Descriptors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A backend can generate specialized domain blocks directly in TOML by extending parent
templates without writing C++ code:

.. code-block:: toml

   [[blocks]]
   use = "signal/generator"
   type = "chirp"
   label = "Chirp Signal"
   params = { f0 = 1.0, f1 = 10.0, sweep_time = 5.0 }
   config = { f = "sin(2 * pi * (f0 + (f1 - f0) / sweep_time * t) * t)" }

Station Pipeline & Web Stations
-------------------------------

Built-in 3D / 2D Entity System
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The built-in station renderer represents scenes as collections of basic geometric entities:

- `box`: Dimensions :math:`[s_x, s_y, s_z]`, transform :math:`[p_x, p_y, p_z]`, rotation Euler angles :math:`[r_x, r_y, r_z]`, color hex or RGB array.
- `sphere`: Radius :math:`r`, center transform :math:`[p_x, p_y, p_z]`.
- `line`: Segment from :math:`\mathbf{p}_1` to :math:`\mathbf{p}_2`, stroke width, color.
- `image`: Aspect ratio, texture URL or data URI, billboard orientation.

Web Stations (Custom HTML5 / WebGL)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For domains requiring specialized visual presentations (such as GIS mapping, architectural
CAD, or custom HTML5 games), backends can provide a custom web station:

1. Configure `descriptor.toml`:

   .. code-block:: toml

      [station]
      view = "web"
      page = "station/index.html"

2. The C++ engine serves the contents of the backend's `station/` directory over HTTP on the
   backend port.
3. The client embeds the page in a secure, sandboxed iframe.
4. The client injects the communication bridge at `/paperboat/station.js`:

   .. code-block:: html

      <canvas id="canvas"></canvas>
      <script src="/paperboat/station.js"></script>
      <script>
        paperboat.on("entities", (entities) => {
          renderCustomScene(entities);
        });
        paperboat.on("signal", (sig) => {
          updateLiveGauges(sig);
        });
      </script>

The bridge enforces strict security boundaries: the iframe has no parent DOM access and
communicates exclusively through structured message passing.

Project Serialization Format
----------------------------

Projects are serialized as formatted JSON (``*.ppj``):

.. code-block:: json

   {
     "paperboat": 0,
     "backend": "mass-spring",
     "flow": {
       "nodes": [{ "id": "sine-1", "type": "sine", "pos": [120, 80], "params": { "freq": 1.5 } }],
       "edges": [{ "from": ["sine-1", "y"], "to": ["mass-1", "force"] }]
     },
     "station": {
       "objects": [{ "id": "mass-1", "type": "mass", "props": { "m": 2.5, "k": 40.0 } }]
     },
     "variables": [
       { "name": "damping", "type": "number", "value": 0.15, "unit": "N*s/m" }
     ],
     "dashboard": {
       "instruments": [{ "block": "plot-1", "size": "large", "at": [0, 0] }]
     },
     "run": {
       "duration": 15.0
     }
   }

Properties matching default values are omitted, producing clean, compact diffs ideal for
version control in Git.
