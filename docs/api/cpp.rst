C++ API
=======

A C++ backend includes one header, ``<paperboat/sdk.hpp>``, which brings everything below
into namespace ``paperboat`` (``paperboat::Block``, ``paperboat::define_block``,
``paperboat::main``…). Headers it doesn't include may change. Link ``paperboat::core``.

.. cpp:namespace:: paperboat::toolkit

API Quick Reference
-------------------

.. list-table::
   :header-rows: 1
   :widths: 22 28 50

   * - Category
     - Symbol
     - Description
   * - **Definitions**
     - :cpp:struct:`PortDef`
     - Input/output port configuration (id, type, vector units).
   * -
     - :cpp:struct:`VectorComponent`
     - Named component channel within an ordered vector port.
   * -
     - :cpp:struct:`ParamDef`
     - Parameter or property definition (number, key, enum, file).
   * -
     - :cpp:struct:`BlockDef`
     - Complete declarative block definition (type, ports, params, kind).
   * -
     - :cpp:struct:`ObjectDef`
     - Station object template definition, properties, and link slots.
   * - **Values & Context**
     - :cpp:type:`Values`
     - Map of string keys to typed values (numbers, vectors, tables, handles).
   * -
     - :cpp:func:`get_or`
     - Safe parameter or signal extraction helper with fallback default.
   * -
     - :cpp:struct:`Context`
     - Execution context providing clock time ``t``, step ``dt``, and ``log()``.
   * - **Block Subclasses**
     - :cpp:class:`Block`
     - Base computational block owning state and implementing ``step()``.
   * -
     - :cpp:class:`Generator`
     - Signal generator computing :math:`f(t)` from time and parameters.
   * -
     - :cpp:class:`Step`
     - Procedural step block implementing ``enter()`` and ``tick()``.
   * -
     - :cpp:class:`Timed`
     - Timed step block executing for a fixed duration.
   * -
     - :cpp:class:`Expression`
     - Fast mathematical expression evaluator over named variables.
   * - **Station & Geometry**
     - :cpp:class:`StationObject`
     - Equipment object providing ``read()``, ``step()``, and ``entities()``.
   * -
     - :cpp:struct:`Entity`
     - Geometry entity rendered in 3D/2D on the Station page.
   * -
     - :cpp:func:`scene::box`, :cpp:func:`scene::sphere`, :cpp:func:`scene::line`, :cpp:func:`scene::image`, :cpp:func:`scene::rect`, :cpp:func:`scene::circle`, :cpp:func:`scene::rod`, :cpp:func:`scene::spring`
     - 3D and 2D visual geometry entity construction helpers.
   * - **Solvers & Physics**
     - :cpp:class:`Model`
     - Continuous ODE physics model with state derivatives.
   * -
     - :cpp:class:`solve::Solver`
     - Abstract numerical differential equation solver interface.
   * -
     - :cpp:func:`solve::step`
     - Advance state vector across one time step using an ODE solver (e.g. RK4).
   * - **Domain & Server**
     - :cpp:class:`DomainState`
     - Domain-wide shared state updated after topological passes.
   * -
     - :cpp:class:`Registry`
     - Repository of block and station object definitions.
   * -
     - :cpp:func:`define_block`
     - Factory helper binding C++ ``Block`` classes to catalog definitions.
   * -
     - :cpp:func:`define_object`
     - Factory helper binding C++ ``StationObject`` classes.
   * -
     - :cpp:func:`load_descriptor_file`
     - Reads and validates ``descriptor.toml`` against the registry.
   * -
     - :cpp:func:`main`
     - Standard CLI entrypoint launching backend server processes.
   * -
     - :cpp:func:`serve`
     - High-performance WebSocket and HTTP server loop.
   * - **Schedulers**
     - :cpp:class:`Executor`
     - Abstract graph execution engine runner.
   * -
     - :cpp:class:`Run`
     - Active run execution lifecycle and telemetry emitter.

Definitions
-----------

What the UI shows and descriptors refer to. All fields have defaults, so designated
initializers may leave any out.

.. cpp:struct:: PortDef

   A block's input or output.

   .. cpp:member:: std::string id

    .. cpp:member:: std::string type = "signal"

       ``"signal"`` (a value), ``"step"`` (control, in a Step flow) or a shape: ``"image"``,
       ``"points"``, ``"vector"``… A wire joins ports of one type. ``"numbers"`` remains a
       compatible name for numeric vectors.

    .. cpp:member:: std::string unit

       A shared unit, or the fallback unit for vector components without their own.

    .. cpp:member:: std::optional<std::size_t> dimension

       A vector's fixed length. Absent: dynamic length; component metadata also fixes it.

    .. cpp:member:: std::vector<VectorComponent> components

       Optional ordered component labels and units. Names are descriptive; dimensions and
       units, not labels, decide whether two vector ports fit.

    .. cpp:member:: bool broadcast = false

       Set on the signal ports of an :cpp:member:`elementwise <BlockDef::elementwise>` block
       (see :cpp:func:`with_broadcast`): the port also joins a vector port. The engine sets
       it; a block author never does.

.. cpp:struct:: VectorComponent

   .. cpp:member:: std::string id

      The component's label, in vector order.

   .. cpp:member:: std::string unit

      Its optional unit.

.. cpp:function:: PortDef vector_port(std::string id)
.. cpp:function:: PortDef vector_port(std::string id, std::size_t dimension)
.. cpp:function:: PortDef vector_port(std::string id, std::vector<VectorComponent> components)

   Make a ``"vector"`` port. For example,
   ``vector_port("state", {{"x", "m"}, {"angle", "rad"}})`` puts a mixed-unit state on one
   wire while retaining the names and units of its components.

.. cpp:struct:: ParamDef

   A setting on a block (or a property of a station object), edited in the UI.

   .. cpp:member:: std::string id

   .. cpp:member:: double default_value = 0.0

   .. cpp:member:: std::string type = "number"

      Number types (to the block as ``Values``): ``"number"``, ``"boolean"`` (a checkbox; 1
      or 0). Text types (through ``Config``): ``"text"``, ``"key"`` (a ``KeyboardEvent.code``,
      picked by pressing it), ``"file"`` (a path, relative to the descriptor's folder),
      ``"enum"`` (one of ``options``, a dropdown; the block also gets its index in ``Values``),
      ``"variable"`` (the name of a project variable, a picker; for
      :cpp:func:`Context::set_variable`).

   .. cpp:member:: std::vector<std::string> options

      An enum's choices.

   .. cpp:member:: std::string default_text

      The default of a text param.

   .. cpp:function:: bool is_number() const

.. cpp:enum-class:: Kind

   .. cpp:enumerator:: Source

      Pushes into the domain.

   .. cpp:enumerator:: Probe

      Reads it.

   .. cpp:enumerator:: Function

      Computes.

   .. cpp:enumerator:: Monitor

      Shows a value on the Dashboard as an instrument.

.. cpp:struct:: BlockDef

   .. cpp:member:: std::string type

      Unique within a catalog (``"pid"``, ``"pid-theta"``).

   .. cpp:member:: std::string label

   .. cpp:member:: Kind kind = Kind::Function

   .. cpp:member:: std::vector<PortDef> inputs

   .. cpp:member:: std::vector<PortDef> outputs

   .. cpp:member:: std::vector<ParamDef> params

   .. cpp:member:: std::string instrument

      A monitor's instrument: ``"graph"``, ``"display"`` or ``"image"``.

   .. cpp:member:: std::string description

      One line in plain words: what the block does. Shown on its card in the block browser.
      Optional; the built-in templates have one, and a descriptor may set ``description``.

   .. cpp:member:: View view

      How the UI draws the block: a hint, so a block without it is the normal card.
      ``compact`` draws a small plate with a ``glyph`` (a curve name such as ``"gain"`` or a
      few characters) and a caption with the name and ``value`` (an expression of the
      params, ``"{k}"``); ``icon`` (an opaque ``nlohmann::json`` object, ``null`` when
      absent) supplies a token-quantised vector icon (``{w, h, g, ports}``) rendered on the
      plate without raw colours or SVG path strings. Every key is optional: a definition
      inherits its parent's ``view`` and overrides single keys (:cpp:func:`View::overridden_by`).
      The built-in simple maths and logic templates have one; a descriptor may set ``view``.

   .. cpp:member:: bool feedthrough = true

      ``false`` for a block whose outputs this step don't depend on its inputs this step (an
      integrator, a delay). Its :cpp:class:`Block` gives ``outputs()`` during the pass and
      takes its inputs in ``step()`` after it, so a feedback loop may pass through it. A loop
      with no such block is an algebraic loop, and the run fails naming a block in it.

   .. cpp:member:: std::vector<Form> forms

      A magic block's forms. Its ``inputs`` and ``outputs`` are the first form's; its enum
      param ``form`` picks one (see :cpp:func:`magic_def`).

   .. cpp:member:: std::string ports_from

      The id of a text param that decides the inputs: one per sign character in it
      (``"+-+"``: ``a``, ``b``, ``c``; see :cpp:func:`parse_signs`), of the first input's type.
      Empty: the inputs are the definition's. Sum's ``signs``. A wire to an input that no
      longer exists is dropped.

   .. cpp:member:: std::string inputs_from

      The id of a text param whose expression's free names are the inputs: one per identifier in
      it (other than ``pi``, ``e`` and built-in functions, in order of first appearance; see
      :cpp:func:`with_inputs`), of the first input's type. Empty: the inputs are the definition's.
      f(t)'s ``f``. A wire to an input that is removed or renamed is dropped.

   .. cpp:member:: std::string outputs_from

      The id of a text param of names (``"x v theta"``, see :cpp:func:`parse_names`) that are
      the outputs, ids in that order. The first vector input gets one component per name, and
      a ``units`` param (``"m m/s rad"``) gives each a unit by position. Demux's ``names``.
      ``ports_from = "names"`` does the same for the inputs (Mux), giving the first vector
      output the components.

   .. cpp:member:: bool elementwise = false

      The block works on one number at a time and the engine lifts it to vectors (#164). Its
      signal ports also take a vector wire. With only numbers in, nothing changes. With any
      vector in, all vectors must be one length N (else the run fails naming the block and
      port), numbers broadcast to every element, and the block runs as N instances, *lanes*,
      each fed its element and keeping its own state (one integral per element); its outputs
      are vectors of N, labelled like the vector that came in. A param change reaches every
      lane. N is found from the wires when the graph is compiled, so a block without
      feedthrough in a loop knows it before its first step; if the length is known only at
      run time (a vector port with no ``dimension``) such a block fails at compile, and any
      other takes N from its first step. The built-in pointwise ``math`` blocks, f(t) and the
      generators, Integrator, Lag and PID are elementwise; Delay is not (it moves whole
      values). A block that needs the whole vector (a norm, a dot product) declares
      :cpp:func:`vector_port` ports instead.

.. cpp:struct:: Form

   One form of a magic block: the ports it has in that form, such as ``"signal"`` (values in
   and out) or ``"step"`` (a step on a Step flow's path).

   .. cpp:member:: std::string name

   .. cpp:member:: std::vector<PortDef> inputs

   .. cpp:member:: std::vector<PortDef> outputs

.. cpp:function:: std::vector<int> parse_signs(std::string_view text)

   The signs in ``text``, one per input: ``+`` is 1, ``-`` (or ``−``) is -1; anything else is
   skipped. At most 26; none at all reads as ``"++"``.

.. cpp:function:: std::vector<std::string> parse_names(std::string_view text, const std::vector<std::string> &reserved = {})

   The names in ``text``, split on spaces and commas: what a ``names`` param makes ports of.
   Throws ``std::invalid_argument`` on none, a name used twice, one that isn't an identifier,
   one in ``reserved`` (the block's other ports) or more than 64; in a run the block fails.

.. cpp:function:: BlockDef with_broadcast(const BlockDef &def)

   ``def`` with ``broadcast`` set on its signal ports if it is elementwise (what the catalog
   sends), else ``def``.

.. cpp:function:: BlockDef with_signs(const BlockDef &def, std::string_view text)

   The definition with the inputs its ``ports_from`` param (``text``) gives: ids ``a``, ``b``,
   ``c``… of the first input's type. ``def`` itself if it has no ``ports_from``.

.. cpp:function:: BlockDef with_inputs(const BlockDef &def, std::string_view text)

   The definition with the inputs its ``inputs_from`` expression param (``text``) gives: one per
   free name in ``text``, in order of first appearance, of the first input's type. ``def`` itself
   if it has no ``inputs_from``. A wire to a removed or renamed input is dropped.

.. cpp:function:: BlockDef in_form(const BlockDef &def, std::string_view form)

   The definition in form ``form``: that form's ports. ``def`` itself if it has no forms, or no
   form of that name.

.. cpp:struct:: ObjectDef

   A station object type.

   .. cpp:member:: std::string type

   .. cpp:member:: std::string label

   .. cpp:member:: std::string role = "body"

      ``"body"`` or ``"joint"``.

   .. cpp:member:: std::vector<ParamDef> props

      Its properties, edited on the Station page only.

   .. cpp:member:: std::vector<std::string> links

      Slots naming other station objects (``{"a", "b"}``).

   .. cpp:member:: std::optional<BlockDef> block

      The linked block it spawns on the Flow page (same id as the object), if any.

Values
------

.. cpp:struct:: Value : std::variant<double, Vector, Table, std::string, Handle>

   What a wire carries: a number, a vector of numbers, a table (rows, such as points as
   ``[x, z]``), text, or a handle. Any arithmetic type converts to ``double``.

.. cpp:type:: Vector = std::vector<double>

   Ordered numeric data. The port's type (``"vector"``), optional dimension and component
   metadata describe its wire contract.

.. cpp:type:: List = Vector

   Compatibility name for ``Vector``.

.. cpp:type:: Table = std::vector<std::vector<double>>

.. cpp:type:: Values = std::map<std::string, Value>

   Port or param id → value (with a transparent comparator).

.. cpp:type:: Config = std::map<std::string, std::string>

   Text settings: a template's config, and every text param by id.

.. cpp:struct:: Handle

   Data the engine passes along without knowing what it is (an image, a mesh). Shared
   and immutable, so one output can feed many inputs.

   .. cpp:member:: std::string kind

      Agreed between packages; matches the ports' type (``"image"``).

   .. cpp:member:: std::shared_ptr<const void> data

.. cpp:function:: template<class T> Handle make_handle(std::string kind, T value)

.. cpp:function:: double get_or(const Values &values, std::string_view id, double fallback = 0.0)

   The number at ``id``, or ``fallback`` when absent or not a number (an unconnected input
   reads as 0).

.. cpp:function:: template<class T> const T *get_if(const Values &values, std::string_view id)

   The vector/list, table or text at ``id``, or null.

.. cpp:function:: const Vector *get_vector(const Values &values, std::string_view id)

   The numeric vector at ``id``, or null. A Python block reads the same value as a list.

.. cpp:function:: Vector as_vector(const Values &values, std::string_view id)

   The numbers at ``id`` as a vector: a vector as it is, a number as one element, otherwise
   (absent, text…) empty. For a block with a :cpp:func:`vector_port` input that should also
   take a plain number.

.. cpp:function:: template<class T> const T *get_handle(const Values &values, std::string_view id, std::string_view kind)

   The object behind a handle of ``kind`` at ``id``, or null.

.. cpp:function:: std::string get_text(const Values &values, std::string_view id, std::string fallback = {})

What runs
---------

.. cpp:struct:: Context

   What a block sees on each call.

   .. cpp:member:: double t = 0.0

      The run clock, s (0 in a run that evaluates once).

   .. cpp:member:: double dt = 0.0

      The clock step, s.

   .. cpp:member:: const KeySet *keys_down = nullptr

      Keys held down in the UI (``KeyboardEvent.code`` values).

   .. cpp:member:: DomainState *state = nullptr

      The domain's shared state for this run, if it has one.

   .. cpp:member:: std::string_view node

      The id of the block (or station object) being called.

   .. cpp:member:: const Values *variables = nullptr

      The run's variables by name: a number, a boolean as 1 or 0, text for the rest. A
      variable marked ``tunable`` may change between steps (#75); any other is constant for
      the run.

   .. cpp:function:: const Value *variable(std::string_view name) const

      A variable's value now, or null if there is none of that name.

   .. cpp:function:: bool set_variable(std::string_view name, const Value &value) const

      Set the tunable variable ``name`` (#166), as an autotuner or a calibration does: a
      number for a number or boolean variable, text for an enum, text or file one. It takes
      effect at the end of the pass, so every block in a pass sees the same variables;
      formulas over it and params bound to it follow (a block that takes no live change is
      told once in the Console), and the UI keeps the value in the project. Returns false,
      with a warning in the Console, if the variable is unknown, not ``tunable``, a formula,
      of another type, or the value is outside its ``min`` / ``max``.

   .. cpp:function:: void log(std::string_view text, std::string_view level = "info") const

      Write a line to the Console (``info``, ``warn``, ``error``), tagged with ``node``.

   .. cpp:function:: void progress(double fraction) const

      How far a long step has got, 0 to 1 (a filter over a big image, a solve): the UI shows
      it on the block. Optional; call it as often as is useful, the run sends at most ~20 a
      second.

.. cpp:class:: Block

   One instance of a block in a running graph. It owns its state; two instances never
   share anything.

   .. cpp:function:: virtual Values step(const Context &ctx, const Values &in) = 0

      Outputs from inputs. An unconnected input is absent from ``in``. Throw to fail the
      run; the error names this block. For a block without feedthrough it is called after
      the pass, to take the step's inputs; others read ``outputs()``.

   .. cpp:function:: virtual Values outputs() const

      A block without feedthrough (``BlockDef::feedthrough`` false): what it outputs now,
      from its state alone. Unused for other blocks.

   .. cpp:function:: virtual std::vector<Entity> entities() const

      What it draws on the Station after a step (a result to show). Default: nothing.

   .. cpp:function:: virtual bool set_param(std::string_view id, double value)

      The number param ``id`` changed during the run (#75): a ``tunable`` variable it is
      bound to has a new value. Take it and return true. The default returns false: the
      block reads its params only when made. Keep your state: a PID takes new gains and
      carries on with its integral.

   .. cpp:function:: virtual bool stateless() const

      True for a block whose outputs depend on its inputs and the clock alone. When
      ``set_param`` returns false the run then makes it again with the new params. A block
      that is neither takes the change with the next run (the Console says so, once).

.. cpp:class:: Step

   One instance of a block on a Step flow's path (a block with step ports).

   .. cpp:function:: virtual void enter(const Context &ctx, const Values &in)

      A path arrived.

   .. cpp:function:: virtual std::string tick(const Context &ctx, const Values &in) = 0

      Each clock step while the path is here: the step output to leave by, or ``""`` to
      stay (a wait, a move). A hat (step outputs, no step input) is ticked while idle and
      starts a path when it names an output.

   .. cpp:function:: virtual bool may_start() const

      For a hat: whether it may still start a path.

   .. cpp:function:: virtual bool ends_run() const

      True for a block that ends the whole run when a path arrives (Stop).

   .. cpp:function:: virtual Values outputs() const

      Its signal outputs (a progress), read after each clock step; signal blocks see them
      in the next pass, so a wire from a step's output back into it is not a cycle.

.. cpp:class:: StationObject

   One station object in a run: owns its state and properties.

   .. cpp:function:: virtual Values read() const = 0

      The linked block's outputs, from the current state.

   .. cpp:function:: virtual void step(const Context &ctx, const Values &in) = 0

      Advance one clock step under the linked block's inputs.

   .. cpp:function:: virtual std::vector<Entity> entities(const std::string &id, double z) const = 0

      Its shapes, ids prefixed with ``id + "/"``, at depth ``z``.

   .. cpp:function:: virtual void link(std::string_view slot, StationObject &other)

      Another object fills one of its link slots; called once, after every object of
      the station is made.

.. cpp:class:: DomainState

   What a domain's blocks share during a run (a game, a document, a physical system).
   The engine makes one per run (``Backend::state``), hands it to blocks in
   ``Context::state``, steps it after each clocked pass and draws it.

   .. cpp:function:: virtual std::vector<Entity> entities() const

      Every entity it draws (for ``scene``).

   .. cpp:function:: virtual std::vector<Entity> patch() const

      The entities that change (for ``scene.patch``). Default: all.

   .. cpp:function:: virtual std::optional<std::string> step(const Context &ctx)

      Advance after a clocked pass. A message ends the run.

   .. cpp:function:: virtual std::optional<std::string> summary() const

      Logged when a run ends.

.. cpp:struct:: Entity

   One shape on the scene.

   .. cpp:member:: std::string id

   .. cpp:member:: std::string shape = "box"

      ``"box"``, ``"sphere"`` (``size[0]`` = radius), ``"line"`` (``pos`` → ``to``) or ``"image"``.

   .. cpp:member:: std::array<double, 3> pos

   .. cpp:member:: std::array<double, 3> size

   .. cpp:member:: std::array<double, 3> rot

      Radians, applied in x, y, z order.

   .. cpp:member:: std::string color = "#888888"

   .. cpp:member:: std::array<double, 3> scale

   .. cpp:member:: std::array<double, 3> to

      A line's end.

   .. cpp:member:: std::string src

      An image's picture: a URL or a ``data:`` URL.

Kinds of block
--------------

Base classes in ``<paperboat/toolkit/bases.hpp>``: derive and implement only what is
particular. Python has the same (:py:class:`paperboat.Generator`, :py:class:`paperboat.Timed`).

.. cpp:class:: Generator : public Block

   A signal f(t). ``t`` is the input ``t`` when wired (a step's ``progress``), else the run
   clock; it outputs ``value(t)``.

   .. cpp:function:: explicit Generator(std::string output = "y")

   .. cpp:function:: virtual double value(double t) const = 0

.. cpp:function:: BlockDef generator_def(std::string type, std::string label, std::vector<ParamDef> params = {}, std::string output = "y")

.. cpp:class:: Timed : public Step

   A step that does its work over ``seconds``. Each clock step the work moves to the
   ``curve`` input's value (clamped to 0–1; the first frame stays at 0) or, unwired, to the
   share of ``seconds`` gone; the last frame goes to 1 and leaves by ``next``. A steady 0.5
   jumps to half at once, holds, and ends at 1. Outputs ``progress`` (the share of time
   gone), to feed a curve.

   .. cpp:function:: explicit Timed(double seconds)

   .. cpp:function:: virtual void begin(const Context &ctx, const Values &in)

      A path arrived.

   .. cpp:function:: virtual void advance(const Context &ctx, double from, double to) = 0

      Move the work from progress ``from`` to ``to``.

.. cpp:function:: BlockDef timed_def(std::string type, std::string label, std::vector<ParamDef> params = {}, Kind kind = Kind::Function)

   Ports ``in``, ``curve`` → ``next``, ``progress``; adds ``seconds`` (1) if ``params`` lacks it.

.. cpp:function:: BlockDef magic_def(std::string type, std::string label, std::vector<Form> forms, std::vector<ParamDef> params = {})

   A magic block: one block type that changes its ports with its ``form`` param, an enum of
   the forms' names (the first is the default) added in front of ``params``. Its template sets
   both ``make`` (for a form of signal ports) and ``make_step`` (for a form with step ports);
   the engine makes the one the node's form needs. The ``base`` logic blocks are built this
   way:

   .. code-block:: cpp

      Template t;
      t.name = "base/if";
      t.def = magic_def("if", "If", {
          {.name = "signal", .inputs = {{"cond"}, {"a"}, {"b"}}, .outputs = {{"y"}}},
          {.name = "step", .inputs = {{"in", "step"}, {"cond"}},
           .outputs = {{"yes", "step"}, {"no", "step"}}},
      });
      t.make = [](const Values&, const Config&) { return std::make_unique<Choose>(); };
      t.make_step = [](const Values& p, const Config&) { return std::make_unique<Branch>(p); };
      registry.add(std::move(t));

.. cpp:class:: Expression

   An expression over named numbers, compiled once (``<paperboat/toolkit/expression.hpp>``).

   .. cpp:function:: Expression(std::string_view text, std::vector<std::string> variables)

      Throws ``ExpressionError`` saying where (``at 5: unknown function "sinn"``).

   .. cpp:function:: double operator()(std::span<const double> values) const

      Values in ``variables`` order.

Domain tools
------------

What domain code writes with, so it gives its equations and its look, never the stepping
or the entity fields (``paperboat/toolkit/solve.hpp``, ``model.hpp``, ``scene.hpp``, all
through ``sdk.hpp``).

Solvers
~~~~~~~

.. cpp:type:: solve::State = std::vector<double>

   A state of any size, known at run time.

.. cpp:type:: solve::Deriv = std::function<State(double t, const State &x)>

   ``dx/dt`` at ``(t, x)``; for ``discrete``, the next state.

.. cpp:struct:: solve::Options

   ``rk45``'s error control: ``rtol`` (``1e-6``), ``atol`` (``1e-9``) per component, and
   ``max_steps`` (``100000``) sub-steps in one clock step before it gives up.

.. cpp:class:: solve::Solver

   .. cpp:function:: virtual void step(State &x, double t, double dt, const Deriv &f) = 0

      Advance ``x`` from ``t`` to ``t + dt``. Throws ``std::runtime_error`` when ``f``
      returns a state of another size, or ``rk45`` can't meet its tolerances.

   .. cpp:function:: template <std::size_t N> void step(std::array<double, N> &x, double t, double dt, const std::function<std::array<double, N>(double, const std::array<double, N>&)> &f)

      Advance a fixed-size state vector without heap allocations.

.. cpp:function:: std::unique_ptr<solve::Solver> solve::make(std::string_view name, Options options = {})

   A solver by name: ``rk4`` (classic Runge–Kutta, the default), ``rk45`` (Dormand–Prince,
   MATLAB's ``ode45``: adaptive, it sub-steps inside the clock step to meet its tolerance, so
   signals still arrive on the clock), ``rk2`` (Heun), ``euler``, ``discrete`` (a difference
   equation ``x[k+1] = f(t, x[k])``). Throws ``std::invalid_argument`` naming them.

.. cpp:function:: void solve::step(std::string_view solver, State &x, double t, double dt, const Deriv &f, Options options = {})

   One step with a fresh solver of that name.

.. cpp:function:: template <std::size_t N> void solve::step(std::string_view solver, std::array<double, N> &x, double t, double dt, const std::function<std::array<double, N>(double, const std::array<double, N>&)> &f, Options options = {})

   Fixed-size state overload for stepping with a fresh solver of that name.

.. cpp:function:: const std::vector<std::string> &solve::names()

.. cpp:function:: ParamDef solve::param(std::string fallback = "rk4")

   An enum param or prop ``solver``, so the user picks one on the Station page.

.. code-block:: cpp

   paperboat::solve::State x = {1.0, 0.0};
   auto rk45 = paperboat::solve::make("rk45", {.rtol = 1e-8});
   rk45->step(x, ctx.t, ctx.dt, [&](double, const paperboat::solve::State& s) {
     return paperboat::solve::State{s[1], -k / m * s[0]};
   });

Model
~~~~~

.. cpp:class:: Model : public StationObject

   A station object that is a system of differential equations. It gives its initial
   state and ``derivatives()``; the solver advances it each clock step, with the linked
   block's inputs held through the step. It still implements ``read()`` and
   ``entities()``.

   .. cpp:function:: explicit Model(solve::State initial, std::string_view solver = "rk4", solve::Options options = {})

   .. cpp:function:: void set_solver(std::string_view solver, solve::Options options = {})

      Configure or switch the numerical solver for the model.

   .. cpp:function:: virtual solve::State derivatives(double t, const solve::State &x, const Values &in) const = 0

   .. cpp:function:: const solve::State &x() const

      The state now.

   .. cpp:function:: protected solve::State &state()

      For a model that changes its own state between steps (a bounce, a reset).

.. code-block:: cpp

   struct Mass final : paperboat::Model {
     explicit Mass(const paperboat::Values& p)
         : Model({paperboat::get_or(p, "x0"), 0.0}, paperboat::get_text(p, "solver", "rk4")),
           k(paperboat::get_or(p, "k")) {}
     paperboat::solve::State derivatives(double, const paperboat::solve::State& x,
                                         const paperboat::Values& in) const override {
       return {x[1], paperboat::get_or(in, "force") - k * x[0]};
     }
     paperboat::Values read() const override { return {{"x", x()[0]}, {"v", x()[1]}}; }
     std::vector<paperboat::Entity> entities(const std::string& id, double z) const override {
       return {paperboat::scene::rect(id + "/mass", x()[0], 0, 0.4, 0.4, "#2f5d62", 0, z)};
     }
     double k;
   };

Scene builders
~~~~~~~~~~~~~~

Each returns an :cpp:struct:`Entity`. Points are ``scene::Vec`` (``std::array<double, 3>``),
y up; the 2D helpers lie in the x–y plane at depth ``z``, which the 2D view shows.

.. cpp:function:: Entity scene::box(std::string id, Vec pos, Vec size, std::string color = "#888888", Vec rot = {})

   Centred at ``pos``, turned by ``rot`` (radians, about x, then y, then z).

.. cpp:function:: Entity scene::sphere(std::string id, Vec pos, double radius, std::string color = "#888888")

.. cpp:function:: Entity scene::line(std::string id, Vec from, Vec to, std::string color = "#888888")

.. cpp:function:: Entity scene::image(std::string id, Vec pos, double width, double height, std::string src)

.. cpp:function:: Entity scene::rect(std::string id, double x, double y, double width, double height, std::string color = "#888888", double angle = 0, double z = 0)

.. cpp:function:: Entity scene::circle(std::string id, double x, double y, double radius, std::string color = "#888888", double z = 0)

.. cpp:function:: Entity scene::rod(std::string id, Vec a, Vec b, double thickness, std::string color = "#888888")

   A bar from ``a`` to ``b``: a link, a pole, a strut.

.. cpp:function:: std::vector<Entity> scene::spring(const std::string &id, Vec a, Vec b, int coils = 8, double width = 0.2, const std::string &color = "#888888")

   A zigzag of lines (ids ``id/0``, ``id/1``…), the same number at any length, so a patch
   moves them all.

Registering
-----------

.. cpp:struct:: Template

   A reusable block: a definition and a factory. Named ``"<package>/<block>"``.

   .. cpp:member:: std::string name

   .. cpp:member:: BlockDef def

   .. cpp:member:: Config config

   .. cpp:member:: std::function<std::unique_ptr<Block>(const Values&, const Config&)> make

      Set for a signal block…

   .. cpp:member:: std::function<std::unique_ptr<Step>(const Values&, const Config&)> make_step

      …or for a block on a Step flow's path.

   .. cpp:member:: bool extendable = false

      A parent for TOML definitions: a ``use`` of it may add params and set any config.

   .. cpp:member:: bool abstract = false

      Only a parent: ``import`` leaves it out.

   .. cpp:member:: std::function<void(const BlockDef&, const Config&)> check

      Validates a definition at load; throws with what's wrong.

.. cpp:struct:: StationTemplate

   A reusable station object type: ``name``, ``def`` (an :cpp:struct:`ObjectDef`) and
   ``make(props)``.

.. cpp:function:: template<class B> Template define_block(const std::string &package, BlockDef def, Config config = {})

   A template for ``B`` (derived from :cpp:class:`Block` or :cpp:class:`Step`), constructed
   from ``(params, config)``, ``(params)`` or nothing. Named ``package + "/" + def.type``.

   .. code-block:: cpp

      registry.add(paperboat::define_block<Count>("parts", {
          .type = "count", .label = "Count parts",
          .inputs = {{"mask", "image"}, {"photo", "image"}},
          .outputs = {{"total"}, {"marked", "image"}},
          .params = {{"min_area", 150.0}}}));

.. cpp:function:: template<class O> StationTemplate define_object(const std::string &package, ObjectDef def)

   A station template for ``O`` (derived from :cpp:class:`StationObject`), constructed
   from ``(props)`` or nothing.

.. cpp:class:: Registry

   Templates and station templates by name.

   .. cpp:function:: void add(Template t)

      Throws ``std::invalid_argument`` on a duplicate name.

   .. cpp:function:: void add(StationTemplate t)

   .. cpp:function:: void add_connector(std::string package, std::string name, nlohmann::json def)

      Registers a Modelica connector definition (such as ``"RealInput"`` or ``"Pin"``) under
      ``package`` so descriptors importing ``package`` (or whose ``domain`` is ``package``)
      carry it in ``catalog.connectors``.

   .. cpp:function:: const Template *find(std::string_view name) const

   .. cpp:function:: std::vector<std::string> packages() const

.. cpp:function:: const Registry &builtins()

   Every built-in template (packages ``base``, ``signal``, ``math``, ``dashboard``,
   ``control``, ``step``). Copy it and add the domain's own; optional external toolkits such
   as ``opencv`` (``paperboat-toolkit-opencv``) register with ``paperboat::toolkit::opencv::register_opencv``.

Loading
-------

.. cpp:struct:: Catalog

   A descriptor loaded against a registry: the app's blocks and station objects, its
   top-level ``connectors`` table (passed unchanged into ``catalog``), its starters
   (``graph``, ``station``, as protocol JSON text) and its Station view
   (``station_view``, ``station_page``). Its entries point into the registry, which must
   outlive it.

.. cpp:function:: Catalog load_descriptor_file(const std::filesystem::path &path, const Registry &registry = builtins())

   Throws ``DescriptorError`` with where the mistake is.

.. cpp:function:: Catalog load_descriptor(std::string_view toml_text, const Registry &registry = builtins())

.. cpp:function:: void define_blocks(Registry &registry, const std::string &package, std::string_view toml_text)

   Blocks defined in TOML on parents in ``registry``, added as ``"<package>/<type>"``: how a
   package ships blocks that are only a definition.

.. cpp:function:: std::string descriptor_domain(std::string_view toml_text)

   The ``domain`` name, before loading (a host that registers the domain's templates
   needs the package name first).

.. cpp:namespace:: paperboat::core

Serving
-------

.. cpp:struct:: RunSettings

   .. cpp:enum-class:: Mode

      .. cpp:enumerator:: Clock

         Step everything on a clock (a Signal flow; a Step flow always does).

      .. cpp:enumerator:: Once

         Evaluate a Signal flow once (a build, a query, an image pipeline).

   .. cpp:member:: Mode mode = Mode::Clock

   .. cpp:member:: double dt = 0.01

   .. cpp:member:: double duration = 10.0

      Run length (a Step flow: its limit), s.

   .. cpp:member:: int emit_every = 4

      Send signals and scene patches every N steps.

   .. cpp:member:: bool real_time = true

      Pace at wall-clock speed (``--run`` and tests turn it off).

   .. cpp:member:: double speed = 1.0

      Times real time when paced; 0: as fast as it can. The UI may change it per run,
      with ``duration`` and ``dt`` (``graph.run``'s ``settings``).

.. cpp:function:: RunSettings with_overrides(RunSettings base, const json &settings)

   ``base`` with a ``graph.run``'s ``settings`` on top (any but ``mode``). Throws
   ``std::invalid_argument`` for a value out of range; a run with one fails before it starts.

.. cpp:struct:: Backend

   What a backend is made of.

   .. cpp:member:: std::string engine

      Its name in ``hello``.

   .. cpp:member:: const toolkit::Catalog *catalog

   .. cpp:member:: std::function<std::unique_ptr<toolkit::DomainState>()> state

      A fresh domain state per run (and one for the opening scene); empty if none.

   .. cpp:member:: json view

      The scene's camera hint, e.g. ``{"mode": "2d", "center": [0, 0], "height": 6}``.

   .. cpp:member:: RunSettings run

   .. cpp:member:: std::function<std::unique_ptr<Executor>(const Graph&, const RunSettings&)> executor

      Optional: a run style of the backend's own for this graph (return ``nullptr`` to leave
      it to :cpp:func:`default_executor`). Most backends leave it empty.

.. cpp:function:: int main(const Backend &backend, int argc, char **argv, int default_port)

   What a backend's ``main()`` returns: ``--port N`` serves the protocol (and a web
   station) there until the process ends; ``--run`` runs the starter once, headless, and
   prints every message as a JSON line (exit 0 when done, 1 when failed).

.. cpp:function:: int serve(const Backend &backend, int port)

.. cpp:function:: std::string run_starter(const Backend &backend, std::ostream &out)

.. cpp:class:: Server

   The server, started and stopped by its owner (a host with its own main loop, such as
   the Python bindings): ``start()`` returns ``""`` when listening, else why not; ``stop()``.

.. cpp:function:: std::string run(const std::string &id, const json &graph, const json &station, const Backend &backend, const Emit &emit, const KeysNow &keys, const std::stop_token &stop)

   One run, without sockets: every event goes to ``emit``. Returns ``"done"``, ``"failed"``
   or ``"cancelled"``. For tests (with ``real_time = false``).

Run styles
----------

``<paperboat/core/executor.hpp>``. A backend needn't know about these: the core picks the
executor for each graph (:cpp:func:`default_executor`). They are for a backend whose runs
fit none of the three, such as an event-driven one or an external solver. Its executor
drives the same :cpp:class:`Run`, so blocks, monitors, the station and the UI work as
always.

.. cpp:class:: Executor

   .. cpp:function:: virtual std::string execute(Run &run) = 0

      Runs ``run`` to its end. Returns ``"done"``, or ``"cancelled"`` once
      ``run.cancelled()`` said so. Throws to fail the run (a ``GraphError`` names the block
      at fault).

.. cpp:function:: std::unique_ptr<Executor> clock_executor()

   A Signal flow on a clock: every block once per ``dt``, for ``duration``.

.. cpp:function:: std::unique_ptr<Executor> once_executor()

   A Signal flow evaluated once.

.. cpp:function:: std::unique_ptr<Executor> step_executor()

   A Step flow: hats start paths, each path is at one block.

.. cpp:function:: std::unique_ptr<Executor> default_executor(const Graph &graph, const RunSettings &settings)

   Steps if the graph has step blocks or stories, else once or clock by ``settings.mode``.

.. cpp:class:: Run

   A run in progress, as an executor sees it: the graph made, the station, the domain
   state and the outputs so far.

   .. cpp:function:: void at_time(double t)

      Sets the run clock and reads the keys held in the UI now.

   .. cpp:function:: json pass()

      Every signal block once, in order. Returns the monitors' samples, for ``frame``.

   .. cpp:function:: std::optional<std::string> advance()

      Station objects, then the domain state, move on. A message if the domain ends the run.

   .. cpp:function:: void frame(const json &samples, std::optional<double> progress)

      Sends samples, the scene's changes and the run's progress; waits to keep real time.

   .. cpp:function:: bool cancelled()

   .. cpp:function:: void log(std::string_view level, std::string_view text)

   .. cpp:function:: void node_state(std::string_view node, std::string_view state)

   .. cpp:function:: const RunSettings &settings() const

   A minimal executor:

   .. code-block:: cpp

      class TenPasses final : public paperboat::Executor {
       public:
        std::string execute(paperboat::Run& run) override {
          for (int k = 0; k < 10; ++k) {
            if (run.cancelled()) return "cancelled";
            run.at_time(k * 0.5);
            run.frame(run.pass(), k / 9.0);
          }
          return "done";
        }
      };

.. cpp:namespace:: paperboat::modelica

Modelica importer
-----------------

``<paperboat/modelica/import.hpp>`` (built when ``omc`` is found at configure time; optional
target ``paperboat::modelica``). Resolves connectors across ``extends``, quantises
``Placement(iconTransformation)`` onto 5-unit edge slots with collision spreading, and
quantises ``Icon(graphics)`` onto Paperboat's design-token primitive set (``view.icon``).

.. cpp:struct:: ImportedModel

   Result of importing one Modelica class: ``icon`` (token-only ``IconDef`` JSON),
   ``connectors`` (short class name → :cpp:struct:`ConnectorDef`), and ``notice``
   (:cpp:struct:`ImportNotice` counting simplified items, dropped items such as ``Bitmap`` or
   ``DynamicSelect``, and ports moved during edge-slot collision spreading). ``to_json()``
   returns ``{"icon", "connectors", "notice"}``.

.. cpp:struct:: ConnectorDef

   A resolved Modelica connector class: ``kind`` (``"acausal"``, ``"input"``, ``"output"``,
   ``"expandable"``), ``handle`` (``"square"``, ``"diamond"``, ``"ring2"``, ``"triangle"``,
   ``"circle"``), ``alt``, ``size``, ``weight``, ``dash``, and variable/unit lists
   (``potential``, ``flow``, ``stream``).

.. cpp:struct:: IconPort

   One connector port placed on an icon plate's edge: ``id``, ``class_name``, ``side``
   (``"left"``, ``"right"``, ``"top"``, ``"bottom"``), ``at`` (``0..100`` in steps of 5),
   plus optional ``variant``, ``array`` (vector count parameter), and ``condition``
   (serialised as ``"if"``).

.. cpp:struct:: ImportNotice

   Simplification report: ``simplified`` count, ``dropped`` record names, and ``moved``
   ports with their shift distance along the edge.

.. cpp:function:: ImportedModel import_model(Omc &omc, const std::filesystem::path &source, std::string_view class_name = {})

   Imports a ``.mo`` file (optional ``class_name`` selects among classes in the file) or, when
   ``source`` is empty, a loaded or MSL qualified class name. Powers
   ``paperboat modelica icon``.

.. cpp:function:: ImportedModel import_class(Omc &omc, std::string_view class_name)

   Imports a loaded or Modelica Standard Library class (loading ``Modelica`` automatically when
   ``class_name`` starts with ``Modelica.``).

.. cpp:function:: void resolve_connectors(Omc &omc, std::string_view class_name, std::vector<RawPort> &out_ports, std::map<std::string, ConnectorDef> &out_connectors, ImportNotice &notice)

   Walks ``class_name`` and its ``extends`` bases to collect public non-``outer`` connector
   instances and map their connector classes to the built-in handle table.

.. cpp:function:: std::vector<IconPort> quantise_ports(const std::vector<RawPort> &raw, ImportNotice &notice, IconExtent extent = {})

   Snaps raw ``iconTransformation`` placements onto ``{side, at}`` (multiples of 5 in
   ``0..100``, rotation snapped to 90°, mirroring kept) and spreads collisions across free
   slots on the same side.

.. cpp:function:: nlohmann::json quantise_icon(const std::vector<OmValue> &icon_annotations, const std::vector<IconPort> &ports, ImportNotice &notice, IconExtent *out_extent = nullptr)

   Quantises ``getIconAnnotation`` records (``Line``, ``Polygon``, ``Rectangle``, ``Ellipse``,
   ``Text``) onto the ``0..100`` 5-unit grid and design-token stroke/fill/weight/font tables.
