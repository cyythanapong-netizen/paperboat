Tutorials
=========

This guide walks you through building applications on the Paperboat framework from the ground
up. Starting with a simple backend, you will learn how to define custom blocks, create 3D
station objects, configure starter flows, and lay out live instruments on the Board.

Tutorial 1: Creating Your First Backend
---------------------------------------

In this tutorial, we will construct a **Reservoir Tank** backend. The system models a liquid
tank where liquid flows in at the top, accumulates according to the tank's cross-sectional
area, and drains through an outlet valve at the base.

Project Layout
~~~~~~~~~~~~~~

Every backend is self-contained in its own directory:

.. code-block:: text

   reservoir/
   ├── descriptor.toml      # App configuration, palette imports, and starters
   └── server.py            # Python backend (or main.cpp for C++)

Writing the Descriptor
~~~~~~~~~~~~~~~~~~~~~~

The :file:`descriptor.toml` tells Paperboat what your application offers, which toolkits to
import, and how the initial workspace opens.

Create :file:`reservoir/descriptor.toml`:

.. code-block:: toml

   domain = "reservoir"

   # Built-in packages, plus the optional control toolkit that provides the PID
   # (`paperboat toolkit install control --repo https://github.com/paperboatlab/paperboat-toolkit-control.git`)
   import = ["base", "control", "dashboard", "math", "signal", "step", "string"]

   # Default run settings (10 seconds duration, 100 Hz step rate)
   [run]
   duration = 10.0
   dt = 0.01

   # Camera view for the Station page
   [station]
   view = { mode = "3d", center = [0.0, 1.0, 0.0], distance = 4.5 }

Starting the Backend
~~~~~~~~~~~~~~~~~~~~

We can write the backend in either Python or C++.

**In Python (`server.py`)**:

.. code-block:: python

   from pathlib import Path
   import paperboat

   DESCRIPTOR = Path(__file__).parent / "descriptor.toml"

   if __name__ == "__main__":
       catalog = paperboat.load_descriptor(DESCRIPTOR)
       paperboat.serve(catalog, port=8780)

**In C++ (`main.cpp`)**:

.. code-block:: cpp

   #include <paperboat/sdk.hpp>

   int main(int argc, char** argv) {
     static const paperboat::Registry registry = paperboat::builtins();
     static const auto catalog = paperboat::load_descriptor_file(PAPERBOAT_DESCRIPTOR, registry);
     return paperboat::main({.engine = "reservoir", .catalog = &catalog}, argc, argv, 8780);
   }

Run the server and open the web application at :code:`http://localhost:5173` (or your local
Paperboat host). You will see an empty canvas with the imported standard toolkit blocks
available in the block browser!

Tutorial 2: Making Custom Blocks
--------------------------------

Now, let's create a custom **Tank Dynamics** block. This block owns its state across simulation
clock steps, integrates incoming flow, and applies orifice discharge physics.

Block Specification
~~~~~~~~~~~~~~~~~~~

- **Type**: :code:`reservoir/tank`
- **Label**: :code:`Tank`
- **Inputs**: :code:`inflow` (:math:`\text{m}^3/\text{s}`)
- **Outputs**: :code:`level` (:math:`\text{m}`), :code:`outflow` (:math:`\text{m}^3/\text{s}`)
- **Params**:
  - :code:`area`: Cross-sectional area in :math:`\text{m}^2` (default: 1.0)
  - :code:`discharge`: Outlet discharge coefficient (default: 0.15)
  - :code:`max_level`: Maximum tank capacity before overflow (default: 3.0)

Implementing in Python
~~~~~~~~~~~~~~~~~~~~~~

Create :file:`reservoir/tank_block.py`:

.. code-block:: python

   import math
   import paperboat

   class TankBlock(paperboat.Block):
       type = "tank"
       label = "Tank Dynamics"
       inputs = ["inflow"]
       outputs = ["level", "outflow"]
       params = {
           "area": 1.0,
           "discharge": 0.15,
           "max_level": 3.0,
       }

       def __init__(self, params):
           super().__init__(params)
           self.level = 0.0  # internal liquid height

       def step(self, ctx, in_values):
           area = max(self.params["area"], 0.01)
           c_d = self.params["discharge"]
           max_h = self.params["max_level"]

           # Inflow from connected wire (defaults to 0.0 when unwired)
           q_in = in_values.get("inflow", 0.0)

           # Torricelli's orifice outflow law: q_out = c_d * sqrt(2 * g * h)
           g = 9.81
           q_out = c_d * math.sqrt(2.0 * g * max(self.level, 0.0))

           # Differential equation: d(level)/dt = (q_in - q_out) / area
           d_level = (q_in - q_out) / area
           self.level = max(0.0, self.level + d_level * ctx.dt)

           # Send diagnostics to the Console if overflow occurs
           if self.level >= max_h:
               ctx.log(f"Tank overflowing: {self.level:.2f}m >= {max_h:.2f}m", "warn")

           return {
               "level": self.level,
               "outflow": q_out,
           }

Implementing in C++
~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include <cmath>
   #include <paperboat/sdk.hpp>

   struct TankBlock final : paperboat::Block {
     explicit TankBlock(const paperboat::Values& params)
         : area_(std::max(0.01, paperboat::get_or(params, "area", 1.0))),
           c_d_(paperboat::get_or(params, "discharge", 0.15)),
           max_h_(paperboat::get_or(params, "max_level", 3.0)) {}

     paperboat::Values step(const paperboat::Context& ctx, const paperboat::Values& in) override {
       const double q_in = paperboat::get_or(in, "inflow", 0.0);
       constexpr double g = 9.81;
       const double q_out = c_d_ * std::sqrt(2.0 * g * std::max(level_, 0.0));

       const double d_level = (q_in - q_out) / area_;
       level_ = std::max(0.0, level_ + d_level * ctx.dt);

       if (level_ >= max_h_) {
         ctx.log("Tank overflowing: " + std::to_string(level_) + "m", "warn");
       }

       return {{"level", level_}, {"outflow", q_out}};
     }

     double area_;
     double c_d_;
     double max_h_;
     double level_ = 0.0;
   };

Registering the Block
~~~~~~~~~~~~~~~~~~~~~

In Python, add the block to your server registry:

.. code-block:: python

   from tank_block import TankBlock

   registry = paperboat.Registry.builtins()
   registry.add(TankBlock)
   catalog = paperboat.load_descriptor(DESCRIPTOR, registry=registry)
   paperboat.serve(catalog, port=8780)

In C++:

.. code-block:: cpp

   paperboat::Registry registry = paperboat::builtins();
   registry.add(paperboat::define_block<TankBlock>(
       "reservoir",
       {.type = "tank",
        .label = "Tank Dynamics",
        .inputs = {{"inflow"}},
        .outputs = {{"level"}, {"outflow"}},
        .params = {{.id = "area", .default_value = 1.0},
                   {.id = "discharge", .default_value = 0.15},
                   {.id = "max_level", .default_value = 3.0}}}));

Tutorial 3: Creating and Configuring a Station
----------------------------------------------

A **Station Object** gives physical presence to your system on the Station page. It declares
user-editable physical properties, 3D visualization shapes, and spawns a **linked block**
on the Flow page.

Defining a Station Object
~~~~~~~~~~~~~~~~~~~~~~~~~

Let's define a 3D visual tank in :file:`reservoir/station_tank.py`:

.. code-block:: python

   import paperboat

   class StationTank(paperboat.StationObject):
       type = "tank_station"
       label = "Reservoir Cylinder"

       # Editable on the Station page only
       props = {
           "radius": 0.5,
           "height": 2.0,
           "color": "#3b82f6",
       }

       # The linked block on the Flow page:
       # Flow pushes inflow in, and reads level out.
       linked = {
           "inputs": ["inflow"],
           "outputs": ["level"],
       }

       def __init__(self, props):
           super().__init__(props)
           self.level = 0.5

       def read(self):
           """Outputs produced for the linked block before the pass."""
           return {"level": self.level}

       def step(self, ctx, in_values):
           """Update state during the simulation step."""
           q_in = in_values.get("inflow", 0.0)
           area = 3.14159 * (self.props["radius"] ** 2)
           q_out = 0.1 * (self.level ** 0.5)
           self.level = max(0.0, self.level + ((q_in - q_out) / area) * ctx.dt)

       def entities(self, obj_id, z=0):
           """Generate 3D shapes for the Scene."""
           r = self.props["radius"]
           h = self.props["height"]
           water_h = min(self.level, h)

           return [
               # Outer transparent tank wall (box or cylinder representation)
               {
                   "id": f"{obj_id}-wall",
                   "type": "box",
                   "pos": [0.0, h / 2.0, 0.0],
                   "size": [r * 2, h, r * 2],
                   "color": "#94a3b8",
                   "wireframe": True,
               },
               # Inner water level (dynamic height scaling)
               {
                   "id": f"{obj_id}-water",
                   "type": "box",
                   "pos": [0.0, water_h / 2.0, 0.0],
                   "size": [r * 1.95, max(water_h, 0.01), r * 1.95],
                   "color": self.props["color"],
               },
           ]

Configuring Starter Objects in `descriptor.toml`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add the starter station object to :file:`reservoir/descriptor.toml`:

.. code-block:: toml

   [[objects]]
   use = "reservoir/tank_station"
   type = "tank_station"
   label = "Main Reservoir"
   defaults = { radius = 0.6, height = 2.5, color = "#2563eb" }

   [station]
   view = { mode = "3d", center = [0.0, 1.25, 0.0], distance = 5.0 }
   objects = [
     { id = "tank-1", type = "tank_station", props = { height = 2.5 } }
   ]

Tutorial 4: Building a Closed-Loop Flow & Dashboard
---------------------------------------------------

Now we will connect a feedback control loop:
1. A **Setpoint** block requests a target water level (1.5 meters).
2. A **PID Controller** computes the required inflow valve position based on the error.
3. The control effort feeds the **Reservoir** linked block.
4. **Monitor** blocks stream telemetry directly to the **Board**.

Starter Graph in `descriptor.toml`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can declare this entire flow as the starter graph in :file:`descriptor.toml`:

.. code-block:: toml

   [graph]
   nodes = [
     # 1. Step setpoint: switches from 0.5m to 1.5m at t = 2s
     { id = "setpoint", type = "step", params = { at = 2.0, to = 1.5, initial = 0.5 }, pos = [60, 120] },

     # 2. Sum junction: error = setpoint - level
     { id = "err", type = "sum", params = { signs = "+-" }, pos = [240, 120] },

     # 3. PID controller: adjusts inflow valve based on error
     { id = "pid", type = "pid", params = { kp = 2.5, ki = 0.8, kd = 0.1 }, pos = [380, 120] },

     # 4. Station Linked Block: automatically appears matching the station object
     { id = "tank-1", type = "tank_station", pos = [540, 120] },

     # 5. Monitor blocks for live telemetry
     { id = "plot-level", type = "graph", label = "Water Level (m)", pos = [740, 60] },
     { id = "disp-flow", type = "display", label = "Inflow Rate", pos = [740, 180] }
   ]

   edges = [
     { from = ["setpoint", "y"], to = ["err", "a"] },
     { from = ["tank-1", "level"], to = ["err", "b"] },
     { from = ["err", "y"], to = ["pid", "error"] },
     { from = ["pid", "u"], to = ["tank-1", "inflow"] },
     { from = ["tank-1", "level"], to = ["plot-level", "in"] },
     { from = ["pid", "u"], to = ["disp-flow", "in"] }
   ]

Laying Out the Dashboard
~~~~~~~~~~~~~~~~~~~~~~~~

Configure default instruments in the :file:`descriptor.toml`:

.. code-block:: toml

   [dashboard]
   instruments = [
     { block = "plot-level", size = "large", at = [0, 0] },
     { block = "disp-flow", size = "compact", at = [2, 0] }
   ]

   sections = [
     { title = "Reservoir Telemetry", at = [0, 0], span = [3, 2] }
   ]

Tutorial 5: Running the System
------------------------------

1. Launch your backend:

   .. code-block:: bash

      python3 reservoir/server.py

2. Open the web interface.
3. Switch to **Commander Mode** (:kbd:`Alt` + :kbd:`M`):

   - On the left, observe the 3D visual tank and its water level.
   - On the right, observe the closed-loop control graph.
   - Drag up the Board drawer at the bottom to watch the live oscilloscope plot.

4. Click :kbd:`▶ Run` (or press :kbd:`Space`):

   - At :math:`t = 2\,\text{s}`, the step block steps to :math:`1.5\,\text{m}`.
   - The PID controller reacts, increasing the inflow valve rate.
   - The 3D water geometry rises smoothly in real time while the oscilloscope plots the
     settling curve.

Congratulations! You have created a complete, end-to-end Paperboat application with custom
physics, 3D visualization, closed-loop control, and real-time telemetry.

Tutorial 6: One Wire for a Whole State
--------------------------------------

A flow can carry several numbers on one wire as a **vector**. Here three numbers are joined,
scaled as one and split again; no custom code is needed.

1. Add three **Number** blocks (``constant``) with values 1, 2 and 3.
2. Add a **Mux** and set its ``names`` param to ``x v theta`` (and ``units`` to
   ``m m/s rad`` if you want the units checked). It now has inputs ``x``, ``v`` and
   ``theta``; wire the numbers into them. Its output ``y`` is a vector of 3, labelled
   ``[3]`` on the card.
3. Add a **Gain** with ``k = 2`` and wire the Mux's ``y`` into its ``u``. Gain is
   elementwise: it scales each element, and its output is a vector of 3 too.
4. Add a **Demux** with the same ``names`` and wire the Gain into its ``u``. Its outputs
   ``x``, ``v``, ``theta`` read 2, 4 and 6; wire any of them to a **Display**.
5. Wire the Gain into a **Graph** as well: in a clock run it draws one trace per element.

Put an **Integrator** after the Gain and you integrate all three at once, each with its own
state; feed the Integrator back through a **Sum** and the loop still works, because the
vector length is found from the wires before the run. The same vector can drive a station
object whose linked block has a vector input (``vector_port("u", 3)``): one wire, the whole
input.

In descriptor TOML:

.. code-block:: toml

   [graph]
   nodes = [
     { id = "x", type = "constant", params = { value = 1 } },
     { id = "v", type = "constant", params = { value = 2 } },
     { id = "theta", type = "constant", params = { value = 3 } },
     { id = "join", type = "mux", params = { names = "x v theta", units = "m m/s rad" } },
     { id = "k", type = "gain", params = { k = 2 } },
     { id = "split", type = "demux", params = { names = "x v theta" } },
     { id = "angle", type = "display" },
   ]
   edges = [
     { from = ["x", "y"], to = ["join", "x"] },
     { from = ["v", "y"], to = ["join", "v"] },
     { from = ["theta", "y"], to = ["join", "theta"] },
     { from = ["join", "y"], to = ["k", "u"] },
     { from = ["k", "y"], to = ["split", "u"] },
     { from = ["split", "theta"], to = ["angle", "in"] },
   ]

Tutorial 7: From Modelica to a Paperboat backend
------------------------------------------------

A Modelica model can become a backend without writing its equations again. Using
Squero Studio OM's CLI (`squero-om`, in ``squero-studio-om/``):

.. code-block:: bash

   squero-om check DCDrive.mo
   squero-om icon DCDrive.mo
   squero-om icon Modelica.Electrical.Analog.Basic.Resistor
   squero-om generate DCDrive.mo --out dc-drive-cpp/
   paperboat build dc-drive-cpp && paperboat run dc-drive-cpp

``squero-om icon <Model.mo | Qualified.Name> [--class <Name>]`` inspects a local
``.mo`` file or a Modelica Standard Library class and prints ``{icon, connectors, notice}`` as
JSON: inherited connectors resolved through ``extends``, ``iconTransformation`` placements
snapped onto 5-unit edge slots, and ``Icon(graphics)`` primitives quantised onto Paperboat's
design tokens.

``generate`` asks omc for the model's backend DAE (``dumpXMLDAE``) and picks a path, printing
which and why (``--path ode|fmu|auto``, default ``auto``):

- **ode**: the solved equations become plain C++ in a ``paperboat::Model``, advanced by the
  station object's ``solver`` prop (``rk4`` by default). It links only the engine.
- **fmu**: when the model has events (``when``, zero crossings), a torn or implicit system,
  or calls an external function or table, omc builds a co-simulation FMU and the generated
  station object steps it.

What becomes what:

- the model is one **station object**; its props are the top-level ``parameter`` values and
  the ``start`` of each state with ``fixed = true`` (as ``<state>_start``), with units;
- its **linked block** (no feedthrough) takes the top-level ``RealInput``\ s and gives the
  top-level ``RealOutput``\ s plus every variable marked
  ``annotation(__paperboat(probe = true))``, carrying ``view.icon`` (from the model's ``Icon``
  and causal connector ``ports``) and registering ``connectors`` in both C++ and
  ``descriptor.toml``;
- everything else stays inside the compiled model.

``CMakeLists.txt``, ``main.cpp`` and ``domain/`` are rewritten on every generate;
``descriptor.toml`` (a step on each input, a graph on each output), ``backend.toml`` and
``README.md`` are written once and then yours. The studio ships one such backend,
``examples/dc-drive-cpp``, its descriptor edited into a PID speed loop.

The same backend, drawn instead of typed
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``dc-drive-cpp`` was authored in Squero Studio OM, with no ``.mo`` written by hand
except where noted. Start the server from the studio's ``examples/`` so the dock's default export folder
(``dc-drive-cpp``) lands next to the model, then open http://localhost:59201:

.. code-block:: bash

   cd squero-studio-om/examples && ../bin/squero-om dc-drive-cpp/modelica/DCDrive.mo

1. **Place parts.** Filter the library and double-click: ``SignalVoltage``, ``Resistor``,
   ``Inductor``, ``RotationalEMF``, ``Ground``, two ``Inertia`` (rotor, load),
   ``SpringDamper``, ``SpeedSensor``, ``AngleSensor``. Each lands on the canvas and into the
   ``.mo`` through omc.
2. **Wire them.** Drag from port to port: the supply loop ``V1.p – R1.p``, ``R1.n – L1.p``,
   ``L1.n – R2.p``, ``R2.n – V1.n`` and ``V1.n – G1.p``; the shaft ``R2.flange – I1.flange_a``,
   ``I1.flange_b – S1.flange_a``, ``S1.flange_b – I2.flange_a``; both sensors on
   ``I2.flange_b``. A wire the connector rules refuse says why in words.
3. **Expose the interface.** Click a part and press ``+ Input`` on ``V1.v``, ``+ Probe`` on
   ``S2.w`` and ``A1.phi``: they become the top-level ``RealInput v`` and ``RealOutput`` ``w``
   and ``phi``, named for their ports. The dock reads ``balanced · 76 / 76``.
4. **Draw the look.** ``Alt+V``, the *look* tab, plate ``2x1``: ``+ ellipse`` (rotor),
   ``+ line`` (shaft), ``+ ellipse`` (load), ``+ text`` (``M``), each dragged on the 5-unit
   grid. Port placement puts ``v`` on the left and ``w``, ``phi`` on the right. *Save* writes
   the model's ``Icon`` and each port's ``iconTransformation``.
5. **Export.** The dock's *Export backend* writes ``dc-drive-cpp/``. Paperboat shows the
   drawn plate on the Flow page with its ``v``, ``w`` and ``phi`` ports.

Today the popover lists only parameters a part already modifies, and Studio cannot probe a
variable that is not a port: the parameter bindings (``R1(R = R)``, ``L1(L = L, i(start = 0,
fixed = true))``, ...), the top-level ``parameter`` declarations and the armature current
``Real i = L1.i annotation(__paperboat(probe = true))`` were typed into the ``.mo``.

Squero Studio OM, the Modelica editor, talks to omc through ``squero-om``
(or ``paperboat start squero-studio-om``), a WebSocket server speaking ``squero/0``:
``{"id", "op": "omc", "expr"}`` answered by ``{"id", "ok", "result" | "error"}``, plus
``{"op": "status", "state", "version"}`` pushes. Only an allow-list of omc calls passes.

``squero-om [Model.mo]`` (or ``paperboat start squero-studio-om``) also serves the Studio app on the same port: ``--app DIR``,
else a ``squero-studio-om`` checkout next to this repository (its ``dist/``), else the release ``paperboat pkg``
fetched from GitHub into ``~/.local/share/paperboat/apps`` and opens ``Model.mo``, default ``./Untitled.mo``. Beyond ``omc``
it has six ops of its own, written in C++ and confined to the opened model, each answered by
``{"id", "ok": true, "result": {…}}`` or ``{"id", "ok": false, "error"}``:

- ``open`` ``{}`` gives ``{file, model}``, the model the server was started on.
- ``save`` ``{model}`` saves it to its own ``.mo`` (never the Standard Library).
- ``icon`` ``{class}`` gives ``{icon, connectors, notice}``, the imported look of a class
  (what ``squero-om icon`` prints).
- ``set_icon`` ``{model, icon}`` writes an ``IconDef``'s graphics back as the model's ``Icon``
  annotation, the inverse of the importer: the same tokens round-trip.
- ``generate`` ``{source, model, out, dry?, path?}`` is ``squero-om generate``. It first
  pushes ``{"op": "progress", "id", "stage", "fraction"}`` (stage ``check``, ``flatten``,
  ``generate`` or ``build``), then answers ``{model, path, reason, out, notices, files?}``. With
  ``dry`` nothing is written; ``files`` maps each relative path to its text (none on the FMU path).
- ``simulate`` ``{model, stop, vars?}`` runs the model for 500 intervals and answers
  ``{t, series, names}``: ``series`` holds the ``vars`` asked for (every variable when omitted) and
  ``names`` every variable in the result.

The numbered example tutorials (01–10)
=======================================

The ten folders in `paperboat-examples <https://github.com/paperboatlab/paperboat-examples>`_
are a complete curriculum. Each level is a self-contained backend with a ``backend.toml``,
a starter descriptor, domain code, a README and a golden result. Run any of them with
``./bin/paperboat run <number>`` (for example ``run 04``), or check all of them with
``./bin/paperboat check <folder>``.

.. list-table:: Curriculum at a glance
   :header-rows: 1
   :widths: 8 30 8 10 44

   * - Level
     - Folder
     - Port
     - Language
     - Platform feature
   * - 01
     - ``01-smart-thermostat-python``
     - 8765
     - Python
     - Clocked Signal flow, bang-bang hysteresis, Board wave plots
   * - 02
     - ``02-cart-pole-cpp``
     - 8766
     - C++
     - ``StationObject`` in code, PID, Variables formulas
   * - 03
     - ``03-low-poly-island-python``
     - 8767
     - Python
     - One-shot evaluation, list/array wires, block commenting
   * - 04
     - ``04-cocktail-barista-python``
     - 8768
     - Python
     - Step-flow tokens, visual groups, subsystems, E-stop key
   * - 05
     - ``05-rover-lander-python``
     - 8769
     - Python
     - Custom HTML5 web station, manual pilot vs GNC autopilot
   * - 06
     - ``06-robot-pick-place-python``
     - 8770
     - Python
     - ABB-style palletizer, analytical IK, quintic trajectories, fork
   * - 07
     - ``07-airfoil-cfd-python``
     - 8771
     - Python
     - D2Q9 lattice-Boltzmann wind tunnel, lift/drag polars
   * - 08
     - ``08-air-hockey-tracker-cpp``
     - 8772
     - C++
     - OpenCV toolkit image wires, HSV→centroid, air-hockey tracking
   * - 09
     - ``09-modular-synth-cpp``
     - 8773
     - C++
     - Vector wires ``[6]``, per-voice synth modules, Scope and PCM Speaker
   * - 10
     - ``10-train-pantograph-cpp``
     - 8774
     - C++/Modelica
     - Active pantograph regulator, compliant catenary, bypass and arcing

Every tutorial also opens the backend's README next to its code. The Station mockups below
are SVGs in ``paperboat-examples/assets/mockups``.

Tutorial 01: Smart Thermostat
-----------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/01-smart-thermostat-station.svg
   :width: 520px
   :alt: Smart thermostat station mockup

A room loses heat to a cold winter ambient; a baseboard heater is switched by a threshold
block. The starter graph computes ``T_target − T_room``, compares it with zero, and feeds the
heater. Try the ``open_window`` dock variable: the loss coefficient jumps and the heater
runs at full power. The Station is a cutaway of the room with a glowing heater, rising heat
plume, thermostat dial and an operable 3D window.

Tutorial 02: Modernized Cart-Pole
---------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/02-cart-pole-station.svg
   :width: 520px
   :alt: Cart-pole station mockup

The benchmark is retained and rebuilt as a precision instrument: ground rail, rubber end
stops, bearing hub, protractor and a force vector. The C++ domain defines a
``paperboat::StationObject``; two PID blocks (angle and cart position) sum into the cart's
force. In the Variables sheet (``Alt+V``) change ``kp_theta`` and watch ``kd_theta`` follow
the formula ``kp_theta / 5``.

Tutorial 03: Low-Poly Island
----------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/03-low-poly-island-station.svg
   :width: 520px
   :alt: Low-poly island station mockup

A one-shot pipeline generates an archipelago: grid → octave noise → radial falloff →
erosion → tree placement → 3D scene. Wires carry lists and tables, not clocked numbers. Use
``Ctrl+/`` to comment out the vegetation block and ``Ctrl+Shift+Y`` to bypass erosion; the
Station rebuilds immediately. The variables change the seed, octaves, scale and sea level.

Tutorial 04: Cocktail Barista
-----------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/04-cocktail-barista-station.svg
   :width: 520px
   :alt: Cocktail barista station mockup

A Step flow runs a recipe from the bar manager's Excel workbook, ``data/recipes.xlsx``:
``Start → Detect Cup → Pour 1/2/3 → Shake & Chill → Strain & Serve → Stop``. A **Choice**
control on the Board picks Negroni, Spritz or Americano; the pour and shake times come from
the sheet. The ``Shake & Chill`` subsystem opens on its own level, and a second hat lets
**Space** trigger an emergency stop and clean-in-place rinse at any time. The Console records
every pour and the Station shows the multi-valve gantry, shaker and rising glass.

Tutorial 05: Lunar Rover Lander
-------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/05-rover-lander-station.svg
   :width: 520px
   :alt: Lunar lander station mockup

The Station is a custom HTML5/Canvas CRT cockpit (``[station] view = "web"``), not the
built-in scene. The Flow page is the GNC autopilot: a descent-speed profile, terminal
deceleration and horizontal steering to LZ-1. Turn the dashboard switch off (or press
**P**) and fly it with **W/A/D**; the cockpit forwards keys through the web-station bridge.
The cockpit fills the viewport, its controls sit above Paperboat's floating run dock, and the
**⛶** button opens it fullscreen. Drag the dock's **landing_x** slider to move LZ-1 laterally
while the autopilot is flying; the GNC law steers to the new pad. Touchdown must be below
1.5 m/s vertical and 0.5 m/s horizontal with fuel left.

Tutorial 06: ABB-Style Palletizer
---------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/06-robot-pick-place-station.svg
   :width: 520px
   :alt: Robot pick-and-place station mockup

An ABB IRB 460-style palletizer sorts three workpieces. Each move is a quintic S-curve in
Cartesian space; an analytical IK block turns the waypoints into joint angles, and the wide
fork attaches and releases parts onto two pallets. A Repeat block runs the cycle three times,
while a Scope plots the joint rates against their motor limits. Tune ``transfer_seconds`` and
``transfer_lift`` in the Variables sheet. The arm is built from Station primitives; a CAD
mesh import is not part of the Station API yet.

Tutorial 07: Airfoil CFD
------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/07-airfoil-cfd-station.svg
   :width: 520px
   :alt: Airfoil CFD station mockup

A D2Q9 lattice-Boltzmann solver runs around a rotating NACA 0012 profile. The Flow pipeline
builds the mask, sets the inflow, advances the lattice and computes the lift/drag polars.
The Station draws velocity arrows coloured by speed, streamlines around the profile, and
lift/drag vectors. Move ``alpha`` from 0° to 25° in the Variables sheet to see the linear
lift curve and the stall break past 14°.

Tutorial 08: Air-Hockey Puck Tracker
------------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/08-air-hockey-station.svg
   :width: 520px
   :alt: Air-hockey puck tracker station mockup

The optional OpenCV toolkit supplies the vision stages over image wires: HSV range, open,
close and the new Centroid block. A camera block produces a synthetic 320×240 frame of an
air-hockey rink, two mallets and a red puck; the tracker estimates image velocity and the
puck point, and detects rail/mallet direction changes. Three Board Image views show the raw
frame, the mask and the centroid-marked result. The CMake project builds without OpenCV too,
loading a degraded descriptor with a synthetic vision stage.

Tutorial 09: Modular Synthesizer
--------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/09-modular-synth-station.svg
   :width: 520px
   :alt: Modular synth station mockup

Six voices travel together on vector wires ``[6]``: chord CV and gate → VCO → VCA → VCF →
mixer. Each module runs elementwise over the vector, and the Scope receives the filter output
as one channel per component plus the master mix. An Audio Out block renders the same
voices into a PCM vector; press **Click for sound** on the Speaker instrument once to hear
it. Open Lookout mode (``Alt+L``) for the
full-screen Board, and use ``root_note``, ``cutoff`` and ``resonance`` in the Variables sheet
to jam. The Station is a walnut-and-aluminium Eurorack case with VU bars driven by the voice
levels.

Tutorial 10: High-Speed Train Pantograph
----------------------------------------

.. image:: https://raw.githubusercontent.com/paperboatlab/paperboat-examples/main/assets/mockups/10-train-pantograph-station.svg
   :width: 520px
   :alt: Train pantograph station mockup

The capstone couples multibody dynamics with active control. The train accelerates 0→320 km/h
in 25 s; the pantograph head rides a suspension spring/damper against a compliant contact
wire, with aerodynamic uplift growing as v². A PI pneumatic regulator holds the contact force
in the 90–140 N window. Bypass the regulator (Board switch or **B**) to watch the pantograph
sag, the force leave its window and the Console report arcing. ``modelica/
PantographCatenary.mo`` is the acausal Squero Studio OM reference for the same equations.
