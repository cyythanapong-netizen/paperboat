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

The :file:`descriptor.toml` tells Paperboat what your application offers, which standard
toolkits to import, and how the initial workspace opens.

Create :file:`reservoir/descriptor.toml`:

.. code-block:: toml

   domain = "reservoir"

   # Import standard blocks from the built-in toolkit
   import = ["signal", "math", "control", "dashboard"]

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

Tutorial 7: A Backend from a Modelica Model
-------------------------------------------

A Modelica model can become a backend without writing its equations again. With
OpenModelica installed (``omc`` on ``PATH``, which also builds ``engine/modelica``):

.. code-block:: bash

   paperboat modelica check DCMotor.mo
   paperboat modelica generate DCMotor.mo --out dc-motor-cpp/
   paperboat build dc-motor-cpp && paperboat run dc-motor-cpp

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
  ``annotation(__paperboat(probe = true))``;
- everything else stays inside the compiled model.

``CMakeLists.txt``, ``main.cpp`` and ``domain/`` are rewritten on every generate;
``descriptor.toml`` (a step on each input, a graph on each output), ``backend.toml`` and
``README.md`` are written once and then yours. ``examples/dc-motor-cpp`` is one such backend,
its descriptor edited into a PID speed loop.

Squero Studio OM, the Modelica editor, talks to the same omc through ``paperboat squero``
(or ``bin/squero-om``), a WebSocket server speaking ``squero/0``:
``{"id", "op": "omc", "expr"}`` answered by ``{"id", "ok", "result" | "error"}``, plus
``{"op": "status", "state", "version"}`` pushes. Only an allow-list of omc calls passes.
