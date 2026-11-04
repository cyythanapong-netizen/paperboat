C++ API
=======

A C++ backend includes one header, ``<paperboat/sdk.hpp>``, which brings everything below
into namespace ``paperboat`` (``paperboat::Block``, ``paperboat::define_block``,
``paperboat::main``…). Headers it doesn't include may change. Link ``paperboat::core``.

.. contents:: On this page
   :local:
   :depth: 1

.. cpp:namespace:: paperboat::toolkit

Definitions
-----------

What the UI shows and descriptors refer to. All fields have defaults, so designated
initializers may leave any out.

.. cpp:struct:: PortDef

   A block's input or output.

   .. cpp:member:: std::string id

   .. cpp:member:: std::string type = "signal"

      ``"signal"`` (a value), ``"step"`` (control, in a Step flow) or a shape: ``"image"``,
      ``"points"``, ``"numbers"``… A wire joins ports of one type.

.. cpp:struct:: ParamDef

   A setting on a block (or a property of a station object), edited in the UI.

   .. cpp:member:: std::string id

   .. cpp:member:: double default_value = 0.0

   .. cpp:member:: std::string type = "number"

      Number types (to the block as ``Values``): ``"number"``, ``"boolean"`` (a checkbox; 1
      or 0). Text types (through ``Config``): ``"text"``, ``"key"`` (a ``KeyboardEvent.code``,
      picked by pressing it), ``"file"`` (a path, relative to the descriptor's folder),
      ``"enum"`` (one of ``options``, a dropdown; the block also gets its index in ``Values``).

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
      params, ``"{k}"``). Every key is optional: a definition inherits its parent's ``view``
      and overrides single keys. The built-in simple maths and logic templates have one;
      a descriptor may set ``view``.

   .. cpp:member:: bool feedthrough = true

      ``false`` for a block whose outputs this step don't depend on its inputs this step (an
      integrator, a delay). Its :cpp:class:`Block` gives ``outputs()`` during the pass and
      takes its inputs in ``step()`` after it, so a feedback loop may pass through it. A loop
      with no such block is an algebraic loop, and the run fails naming a block in it.

   .. cpp:member:: std::vector<Form> forms

      A magic block's forms. Its ``inputs`` and ``outputs`` are the first form's; its enum
      param ``form`` picks one (see :cpp:func:`magic_def`).

.. cpp:struct:: Form

   One form of a magic block: the ports it has in that form, such as ``"signal"`` (values in
   and out) or ``"step"`` (a step on a Step flow's path).

   .. cpp:member:: std::string name

   .. cpp:member:: std::vector<PortDef> inputs

   .. cpp:member:: std::vector<PortDef> outputs

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

.. cpp:struct:: Value : std::variant<double, List, Table, std::string, Handle>

   What a wire carries: a number, a list of numbers, a table (rows, such as points as
   ``[x, z]``), text, or a handle. Any arithmetic type converts to ``double``.

.. cpp:type:: List = std::vector<double>

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

   The list, table or text at ``id``, or null.

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

   .. cpp:function:: const Template *find(std::string_view name) const

   .. cpp:function:: std::vector<std::string> packages() const

.. cpp:function:: const Registry &builtins()

   Every built-in template (packages ``base``, ``signal``, ``math``, ``dashboard``,
   ``control``, ``step``). Copy it and add the domain's own; the optional ``opencv`` package
   registers with ``paperboat::toolkit::opencv::register_opencv``.

Loading
-------

.. cpp:struct:: Catalog

   A descriptor loaded against a registry: the app's blocks and station objects, its
   starters (``graph``, ``station``, as protocol JSON text) and its Station view
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
