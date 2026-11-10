Toolkit packages
================

Built-in templates, by package (``engine/toolkit``). Import a package in a descriptor
(``import = ["signal", "math"]``) or use one template (``use = "math/gain"``). Paperboat is
not Simulink: ``base`` holds only what any domain needs; everything else lives in the
package of what it is about. List them with ``paperboat-catalog --templates``.

Package Quick Reference
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 18 32 50

   * - Package
     - Block Templates
     - Functional Role
   * - :ref:`pkg-base`
     - ``constant``, ``text``, ``boolean``, ``key``, ``key_hold``, ``to_number``, ``to_text``, ``to_boolean``, ``type_of``, ``mux``, ``demux``, ``now``, ``interval``, ``timeout``, ``delay``, ``if``, ``and``, ``or``, ``xor``, ``not``, ``compare``, ``in_range``
     - Fundamental values, type conversions, runtime clock delays, and dual signal/step logic gates.
   * - :ref:`pkg-signal`
     - ``clock``, ``sine``, ``step``, ``ramp``, ``square``, ``ease``, ``function``, ``noise``, ``generator``, ``fft``
     - Continuous time waveforms :math:`f(t)`, periodic generators, free mathematical expressions, and a spectrum.
   * - :ref:`pkg-math`
     - ``sum``, ``product``, ``gain``, ``bias``, ``min``, ``max``, ``abs``, ``clamp``, ``wrap``, ``map``, ``expression``, ``erf``, ``gamma``, ``bessel``, ``distribution``
     - Arithmetic operators, dynamic multi-sign summing junctions, range boundary transformations, and Boost.Math special functions and distributions.
   * - optimization (external)
     - ``qp``, ``lp``
     - Quadratic and linear programs on OSQP, from a JSON problem file.
   * - opencv (external)
     - ``gray``, ``convert``, ``blur``, ``median``, ``threshold``, ``adaptive_threshold``, ``hsv_range``, ``canny``, ``sobel``, ``laplacian``, ``erode``, ``dilate``, ``open``, ``close``, ``invert``, ``distance``, ``resize``, ``crop``, ``flip``, ``rotate``, ``normalize``, ``equalize``, ``clahe``, ``bitwise``, ``blend``, ``absdiff``, ``background``, ``stats``, ``centroid``, ``contours``, ``hough_lines``, ``hough_circles``, ``template_match``, ``draw``, ``save``, ``view``
     - Image processing on ``image`` wires: geometry, colour, filters, analysis, annotation and output, plus an image instrument and station object.
   * - :ref:`pkg-string`
     - ``string/format``, ``string/concat``, ``string/split``, ``string/replace``, ``string/length``
     - Text formatting, templating, and string manipulation operations.
   * - :ref:`pkg-list`
     - ``range``, ``series``, ``list_length``, ``item``, ``aggregate``, ``points``, ``coordinates``
     - Lists of numbers and of points on the wires: made, measured, indexed, joined, split and reduced.
   * - control (external)
     - ``pid``, ``integrator``, ``lag``, ``delay``, ``mpc``, ``lp_step``, ``nmpc``
     - Clock dynamics and per-step model predictive control; install ``optimization`` for the dynamic blocks.
   * - :ref:`pkg-dashboard`
     - ``graph``, ``display``, ``scope``, ``lamp``, ``image``
     - Telemetry instruments piping signals to real-time oscilloscopes, digital readouts, and visual frames.
   * - :ref:`pkg-step`
     - ``start``, ``when``, ``wait``, ``if``, ``repeat``, ``while``, ``say``, ``join``, ``stop``
     - Discrete event control flow nodes, condition branching, loops, and path synchronization.

Package structure
-----------------

A toolkit is one folder, each block is a folder in it, and everything of a block is in its
folder. Built-in packages live in ``engine/toolkit/src/<package>/``::

   engine/toolkit/src/math/
     toolkit.toml            name = "math"
     arithmetic_shared.hpp   a helper a few blocks share
     gain/
       gain.cpp              the block: defines paperboat::toolkit::math::add_gain(Registry&)
       icon.hpp              its look on the plate: a view.icon
       web/                  its UI, if it has one (see below)
       design.html           its design mockups and notes
     sum/
       sum.cpp
       icon.hpp

CMake finds each ``add_<folder name>`` from the folder names (``<block>/<block>.cpp``) and
generates the package-registration dispatcher, so a new block edits no list, manifest, registry
or description map. A block's implementation, ``BlockDef`` (including ``description``, ``view``
and its ``view.icon`` glyph) and registration stay in its folder. A manifest may add
``definitions = "blocks.toml"`` (``signal``) to embed blocks that are only TOML on a C++ parent in
the same package; they are registered after the folders. An explicit ``register = ["add_x", …]``
in the manifest replaces the folder rule. Delete the folder and the rest still builds and runs;
put it back and it works with no edit elsewhere.

A block that needs its own UI keeps it in its ``web/`` folder
(``engine/toolkit/src/<package>/<block>/web/index.tsx``; an installed toolkit's
``engine/toolkit/<package>/<block>/web/index.tsx``, or ``<package>/web/index.tsx`` for one UI for
the whole package). The web app bundles every such entry and calls its ``register(host)`` export;
``host.registerInstrument(kind,
component, { size, order })`` adds a Dashboard instrument for monitors whose block says
``instrument = kind``. ``size`` is the cell size it opens at (``compact``, ``normal``, ``large``,
``xlarge``; default ``normal``) and ``order`` its place in the automatic order (readouts 0, pictures 1,
plots 2, the default). The component gets ``name``, ``kind``, ``latest``, ``idle``, ``vector``, ``params``,
``send`` and ``state`` (``state.traces`` is the history of every monitor's signals), and from the host
its ``size``, its ``inputs`` and the ``sources`` wired into them, its ``settings`` (a table kept in the
Board layout and saved with the project, set with ``onSettings``) and ``taps`` / ``onTap`` to wire an
output in. A block says how the run samples it with ``BlockDef::whole_vector`` (a vector is one sample)
and ``sample_per_input`` (one sample per wired input, tagged with it). Nothing in the engine or the web
app names the package::

   // engine/toolkit/opencv/view/web/index.tsx
   import type { ToolkitWebHost } from "@paperboat/host";   // the app, as types only
   import "./image.css";                                     // its own styles
   export function register(host: ToolkitWebHost) {
     host.registerInstrument("image", ImageInstrument, { size: "large", order: 1 });
   }

A ``web/`` folder also holds the block's components, CSS, tests (``*.test.ts``, run by the web
app's ``npm test``) and a ``package.json`` naming its own npm dependencies (the folder is an npm
workspace; the scope's names ``uplot``). The entries are bundled at build time: after installing a
toolkit with a ``web/`` folder, run ``npm install`` and rebuild the web app (the dev server picks it
up at once). A kind nothing registered shows a plain tile with its kind and latest value.

Optional toolkits with external dependencies live in ``engine/toolkit/<package>/`` with the same
layout plus a ``toolkit.toml`` and a package-local ``CMakeLists.txt``. The engine discovers the
folder; the package's CMake file decides whether its dependency is available. The backend links and
registers an optional package explicitly.

An optional toolkit can ship 1–3 examples at its repository root. ``toolkits.toml`` gives it a
``prefix`` (``ocv``, ``opt``) and ``paperboat toolkit install <name>`` copies the examples to
``engine/toolkit/<name>/examples/``; ``paperboat backends`` lists them and ``paperboat run ocv-01``
starts the first one. The prefix keeps them distinct from the numbered ``paperboat-examples``
curriculum.

The root ``toolkits.toml`` is a catalog, not an installer: its ``repo`` is a suggested source.
A backend's ``toolkits = [...]`` declares what it needs; ``paperboat check`` verifies the local
install and never downloads. Install from any repository or local folder with
``paperboat toolkit install <name> --repo <url>`` (or ``--path <dir>``).

Optional toolkits never enter an artifact. The engine is Apache-2.0; an optional toolkit carries
its own licence (the mesh toolkit is GPL-3.0). ``.deb``, ``.rpm``, ``.tar.gz``, ``.zip``, the npm
packages, the Python wheel and the web bundle contain no file from ``engine/toolkit/<name>/``,
``share/paperboat/toolkit/`` or a ``*toolkit_<name>*`` library. ``scripts/check_bundle.py`` enforces
this on an install tree and on every package (pb#297), the release script and CI run it, and the
engine's ``ctest`` installs into a scratch prefix and checks that too. Installing one is the user's
choice, made explicitly, and never something a package does for them.

Toolkit packages
================

Built-in templates, by package (``engine/toolkit``). Import a package in a descriptor
(``import = ["signal", "math"]``) or use one template (``use = "math/gain"``). Paperboat is
not Simulink: ``base`` holds only what any domain needs; everything else lives in the
package of what it is about. List them with ``paperboat-catalog --templates``.

Package Quick Reference
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 18 32 50

   * - Package
     - Block Templates
     - Functional Role
   * - :ref:`pkg-base`
     - ``constant``, ``text``, ``boolean``, ``key``, ``key_hold``, ``to_number``, ``to_text``, ``to_boolean``, ``type_of``, ``mux``, ``demux``, ``now``, ``interval``, ``timeout``, ``delay``, ``if``, ``and``, ``or``, ``xor``, ``not``, ``compare``, ``in_range``
     - Fundamental values, type conversions, runtime clock delays, and dual signal/step logic gates.
   * - :ref:`pkg-signal`
     - ``clock``, ``sine``, ``step``, ``ramp``, ``square``, ``ease``, ``function``, ``noise``, ``generator``, ``fft``
     - Continuous time waveforms :math:`f(t)`, periodic generators, free mathematical expressions, and a spectrum.
   * - :ref:`pkg-math`
     - ``sum``, ``product``, ``gain``, ``bias``, ``min``, ``max``, ``abs``, ``clamp``, ``wrap``, ``map``, ``expression``, ``erf``, ``gamma``, ``bessel``, ``distribution``
     - Arithmetic operators, dynamic multi-sign summing junctions, range boundary transformations, and Boost.Math special functions and distributions.
   * - optimization (external)
     - ``qp``, ``lp``
     - Quadratic and linear programs on OSQP, from a JSON problem file.
   * - opencv (external)
     - ``gray``, ``convert``, ``blur``, ``median``, ``threshold``, ``adaptive_threshold``, ``hsv_range``, ``canny``, ``sobel``, ``laplacian``, ``erode``, ``dilate``, ``open``, ``close``, ``invert``, ``distance``, ``resize``, ``crop``, ``flip``, ``rotate``, ``normalize``, ``equalize``, ``clahe``, ``bitwise``, ``blend``, ``absdiff``, ``background``, ``stats``, ``centroid``, ``contours``, ``hough_lines``, ``hough_circles``, ``template_match``, ``draw``, ``save``, ``view``
     - Image processing on ``image`` wires: geometry, colour, filters, analysis, annotation and output, plus an image instrument and station object.
   * - :ref:`pkg-string`
     - ``string/format``, ``string/concat``, ``string/split``, ``string/replace``, ``string/length``
     - Text formatting, templating, and string manipulation operations.
   * - :ref:`pkg-list`
     - ``range``, ``series``, ``list_length``, ``item``, ``aggregate``, ``points``, ``coordinates``
     - Lists of numbers and of points on the wires: made, measured, indexed, joined, split and reduced.
   * - control (external)
     - ``pid``, ``integrator``, ``lag``, ``delay``, ``mpc``, ``lp_step``, ``nmpc``
     - Clock dynamics and per-step model predictive control; install ``optimization`` for the dynamic blocks.
   * - :ref:`pkg-dashboard`
     - ``graph``, ``display``, ``scope``, ``lamp``, ``image``
     - Telemetry instruments piping signals to real-time oscilloscopes, digital readouts, and visual frames.
   * - :ref:`pkg-step`
     - ``start``, ``when``, ``wait``, ``if``, ``repeat``, ``while``, ``say``, ``join``, ``stop``
     - Discrete event control flow nodes, condition branching, loops, and path synchronization.

Package structure
-----------------

A toolkit is one folder, and everything of it is in that folder. Built-in packages live in
``engine/toolkit/src/<package>/``::

   engine/toolkit/src/math/
     toolkit.toml        name = "math"
     gain.cpp            one block, one file: defines paperboat::toolkit::math::add_gain(Registry&)
     sum.cpp
     arithmetic_shared.hpp   a helper a few blocks share
     web/                its UI, if any (see below)

CMake finds each ``add_<file name>`` by the file name and generates the package-registration
dispatcher, so a new block edits no list, manifest, registry or description map. A block's
implementation, ``BlockDef`` (including ``description``, ``view`` and its ``view.icon`` glyph)
and registration stay in its one file. A manifest may add ``definitions = "blocks.toml"``
(``signal``) to embed blocks that are only TOML on a C++ parent in the same package; they are
registered after the files. An explicit ``register = ["add_x", …]`` in the manifest replaces the
file-name rule. Delete the folder and the rest still builds and runs; put it back and it works
with no edit elsewhere.

Optional toolkits with external dependencies live in ``engine/toolkit/<package>/`` with a
``toolkit.toml`` and a package-local ``CMakeLists.txt``. The engine discovers the folder;
the package's CMake file decides whether its dependency is available. The backend links and
registers an optional package explicitly. Install one from any repository or local folder with
``paperboat toolkit install <name> --repo <url>``; the manifest's ``repo`` is only a suggestion.

An optional toolkit can ship 1–3 examples at its repository root. ``toolkits.toml`` gives it a
``prefix`` (``ocv``, ``opt``) and ``paperboat toolkit install <name>`` copies the examples to
``engine/toolkit/<name>/examples/``; ``paperboat backends`` lists them and ``paperboat run ocv-01``
starts the first one. The prefix keeps them distinct from the numbered ``paperboat-examples``
curriculum.

A package that needs its own UI keeps it in a ``web/`` folder next to its C++
(``engine/toolkit/<package>/web/index.tsx`` or ``engine/toolkit/src/<package>/web/index.tsx``). The
web app bundles every such entry and calls its ``register(host)`` export; ``host.registerInstrument(kind,
component, { size, order })`` adds a Dashboard instrument for monitors whose block says
``instrument = kind``. ``size`` is the cell size it opens at (``compact``, ``normal``, ``large``,
``xlarge``; default ``normal``) and ``order`` its place in the automatic order (readouts 0, pictures 1,
plots 2, the default). The component gets ``name``, ``kind``, ``latest``, ``idle``, ``vector``, ``params``,
``send`` and ``state`` (``state.traces`` is the history of every monitor's signals), and from the host
its ``size``, its ``inputs`` and the ``sources`` wired into them, its ``settings`` (a table kept in the
Board layout and saved with the project, set with ``onSettings``) and ``taps`` / ``onTap`` to wire an
output in. A block says how the run samples it with ``BlockDef::whole_vector`` (a vector is one sample)
and ``sample_per_input`` (one sample per wired input, tagged with it). Nothing in the engine or the web
app names the package::

   // engine/toolkit/opencv/web/index.tsx
   import type { ToolkitWebHost } from "@paperboat/host";   // the app, as types only
   import "./image.css";                                     // its own styles
   export function register(host: ToolkitWebHost) {
     host.registerInstrument("image", ImageInstrument, { size: "large", order: 1 });
   }

``web/`` also holds the package's components, CSS, tests (``*.test.ts``, run by the web app's
``npm test``) and a ``package.json`` naming its own npm dependencies (the folder is an npm
workspace; the dashboard's names ``uplot``). The entries are bundled at build time: after installing
a toolkit with a ``web/`` folder, run ``npm install`` and rebuild the web app (the dev server picks it
up at once). A kind nothing registered shows a plain tile with its kind and latest value.

.. _pkg-base:

base
----

Values of each datatype: ``constant`` (Number: a float, param ``value``), ``text`` (Text: a
string) and ``boolean`` (Boolean: a checkbox, 1 or 0 on the wire); all output ``y``. And
``key`` (out ``pressed``: 1 while its ``key`` param's key is held in the UI) and ``key_hold``
(out ``held``: 1 while down; ``duration``: seconds held down so far).

Type conversions and inspection:
- ``to_number``: ``in`` (any), ``fallback`` (signal) → ``y`` (signal). Parses numbers from text/booleans; invalid parses use ``fallback``.
- ``to_text``: ``in`` (any) → ``y`` (text). Formats numbers, booleans, vectors, and handles as clean text.
- ``to_boolean``: ``in`` (any) → ``y`` (signal). Truthiness coercion (numbers != 0, non-empty text, non-empty vectors).
- ``type_of``: ``in`` (any) → ``y`` (text). Returns ``"number"``, ``"text"``, ``"vector"``, ``"table"``, ``"handle"``, or ``"empty"``.

Vectors (#164):

- ``mux``: one signal input per name → ``y`` (vector). Param ``names`` (``"x v theta"``,
  space- or comma-separated; default ``"a b"``) gives the inputs, in order, and the vector's
  dimension and component labels; ``units`` (``"m m/s rad"``, ``-`` for none) gives each a
  unit by position, so units are checked on both sides.
- ``demux``: ``u`` (vector) → one signal output per name. The same ``names`` and ``units``;
  ``u`` has that dimension, so a vector of another length is refused when the wire is drawn.

Bad ``names`` (a name twice, one that isn't an identifier, ``y`` on a Mux or ``u`` on a
Demux, more than 64) fail the run on the block. Between a Mux and a Demux, any
:ref:`elementwise <elementwise>` block works on the whole vector at once.

Time blocks:
- ``now``: → ``y`` (signal: current run clock in milliseconds).
- ``interval``: ``start``, ``stop`` → ``y`` (signal: periodic 1.0 pulses every ``interval`` ms).
- ``timeout``: ``reset`` → ``y`` (signal: becomes 1.0 after ``delay`` ms).
- ``delay``: ``in`` (any) → ``y`` (any): delays input by ``delay`` ms (feedthrough is false).

Logic, as **magic blocks**: each has two forms, picked by its ``form`` param, and takes the
ports of the one picked. In the **signal** form it computes; in the **step** form it sits on
a Step flow's path (``in``) and branches, leaving by ``yes`` when its test holds, else ``no``.
A value holds when it is positive (a key held, a threshold over its level).

.. list-table::
   :header-rows: 1

   * - Block
     - signal form
     - step form
   * - ``if``
     - ``cond``, ``a``, ``b`` → ``y``: ``a`` while ``cond`` holds, else ``b`` (any value)
     - ``in``, ``cond`` → ``yes``, ``no``
   * - ``and``, ``or``
     - ``a``, ``b``… → ``y``: 1 or 0 (multi-input via ``inputs`` param)
     - ``in``, ``a``, ``b``… → ``yes``, ``no``
   * - ``xor``
     - ``a``, ``b`` → ``y``: holds when exactly one input holds
     - ``in``, ``a``, ``b`` → ``yes``, ``no``
   * - ``not``
     - ``u`` → ``y``
     - ``in``, ``u`` → ``yes``, ``no``
   * - ``compare``
     - ``a``, ``b`` → ``y``; ``op``: > >= < <= == !=
     - ``in``, ``a``, ``b`` → ``yes``, ``no``
   * - ``in_range``
     - ``u``, ``min``, ``max`` → ``y``: holds when ``min <= u <= max``
     - ``in``, ``u``, ``min``, ``max`` → ``yes``, ``no``

.. _pkg-signal:

signal
------

Signals f(t). ``t`` is the block's input ``t`` when wired (a step's ``progress``, so the
signal follows the step's own time), else the run clock. All output ``y`` except ``clock``
(out ``t``).

C++: ``clock``, and ``generator``, the abstract parent: f is an expression (config ``f``) over
``t`` and the definition's number params. Defined on it in TOML
(``engine/toolkit/src/signal/blocks.toml``):

.. code-block:: toml

   [[blocks]]
   use = "signal/generator"
   type = "sine"
   label = "Sine"
   params = { amplitude = 1, frequency = 1, phase = 0, offset = 0 }
   config = { f = "offset + amplitude * sin(2 * pi * frequency * t + phase)" }

``sine``, ``step`` (``at``, ``value``), ``ramp`` (``start``, ``slope``), ``square`` (``amplitude``,
``frequency``), ``ease`` (shapes a 0–1 progress; ``shape``: in-out, in, out or linear) and
``function`` (f(t): its ``f`` is a text param, typed on the block; its free names become its
input ports, with ``t`` as the clock when unwired and other unwired inputs reading as 0;
renaming a name in ``f`` drops the wire to the old port). ``fft`` (``u`` → ``magnitude``,
``peak_hz``) keeps the last ``size`` samples (64…1024) and outputs their amplitude spectrum
and the strongest bin's frequency; ``window`` is none, hann or hamming, and ``rate`` 0 reads
the run clock's. A descriptor defines more the same way.
Expressions: numbers, names, ``+ - * / % ^``,
comparisons (1 or 0), ``&& || !``, ``c ? a : b``, ``pi``, ``e`` and ``sin cos tan asin acos atan
atan2 sinh cosh tanh exp log log10 sqrt abs floor ceil round sign frac min max clamp mod
pow``.

.. _pkg-math:

math
----

Pointwise arithmetic and math operations on signals. All but the two random blocks are
:ref:`elementwise <elementwise>`: wire a vector in and each output is a vector.
- ``gain`` (``u`` → ``y``; ``k``)
- ``sum`` (``a``, ``b``… → ``y``; signs param)
- ``arithmetic`` (``a``, ``b`` → ``y``; ``op``: ``+``, ``-``, ``*``, ``/``, ``%``, ``^``, division-by-zero safe)
- ``absolute`` (``u`` → ``y``: absolute value)
- ``round`` (``u`` → ``y``; ``mode``: ``round``, ``floor``, ``ceil``, ``truncate``)
- ``min_max`` (``a``, ``b``… → ``y``; ``mode``: ``min``, ``max``; multi-input via ``inputs`` param)
- ``clamp`` (``u``, ``min``, ``max`` → ``y``)
- ``threshold`` (``u`` → ``y``: 1 above ``level``, else 0)
- ``saturation`` (``u`` → ``y``; ``min``, ``max``)
- ``map_range`` (``u``, ``in_min``, ``in_max``, ``out_min``, ``out_max`` → ``y``; optional ``clamp``)
- ``lerp`` (``a``, ``b``, ``t`` → ``y``: linear interpolation)
- ``random_number`` (``min``, ``max`` → ``y``; params: ``min``, ``max``, ``integer``, ``seed``)
- ``random_boolean`` (``p`` → ``y``; params: ``p``, ``seed``)
- ``erf`` (``u`` → ``y``: error function, Boost.Math)
- ``gamma`` (``u`` → ``y``; ``mode``: ``gamma``, ``log``, ``digamma``)
- ``bessel`` (``u`` → ``y``; ``order``, ``kind``: ``J``, ``Y``, ``I``, ``K``)
- ``distribution`` (``u`` → ``y``; ``family``: ``normal``/``uniform``/``exponential``, ``function``: ``pdf``/``cdf``/``quantile``, ``a``, ``b``: mean and standard deviation, bounds, or rate)

.. _elementwise:

Elementwise blocks
^^^^^^^^^^^^^^^^^^

A block marked ``elementwise`` (a catalog flag; its signal ports carry ``broadcast``) is
written for one number but takes a vector wire too: Gain on a 6-vector scales all six.
With only numbers in, it works as before. With a vector in, every vector into it must
have one length N, numbers broadcast to every element, and the outputs are vectors of N
with the input vector's component labels. Stateful ones keep one state per element: an
Integrator fed a 3-vector holds three integrals, a PID three controllers. The length is
found from the wires before the run, so a vector loop through an Integrator works; if it
can't be (a vector of a length known only at run time feeding an Integrator), the run fails
naming the block. Elementwise: the ``math`` blocks above but the random ones, ``function``
and the ``signal`` generators, ``integrator``, ``lag`` and ``pid``.

.. _pkg-string:

string
------

Text processing and manipulation blocks:
- ``concat``: ``a``, ``b``… (any) → ``y`` (text); joins inputs formatted as text separated by ``separator``. Multi-input via ``inputs`` param.
- ``format``: → ``y`` (text); dynamic socket generation from template placeholders (e.g. ``"{first} {last}"``).
- ``split``: ``text``, ``delimiter`` → ``y`` (text), ``vector`` (vector of parsed numbers), ``count`` (number of tokens).
- ``replace``: ``text``, ``find``, ``replace`` → ``y`` (text); replaces occurrences (all or first).
- ``substring``: ``text``, ``start``, ``length`` → ``y`` (text); safe slicing with negative indexing.
- ``trim``: ``text`` → ``y`` (text); trims whitespace (``mode``: ``both``, ``start``, ``end``).
- ``length``: ``text`` → ``y`` (signal); string character count.
- ``change_case``: ``text`` → ``y`` (text); converts case (``mode``: ``upper``, ``lower``, ``title``).
- ``text_test``: ``text``, ``test`` → ``y`` (signal or step branches ``yes``/``no``; ``mode``: ``contains``, ``starts_with``, ``ends_with``).

.. _pkg-list:

list
----

Lists on the wires (#24). A port of type ``numbers`` carries a list of numbers (any length); one
of type ``points`` a list of points (one row per point, ``[x, z]``). A list wire is drawn heavier
than a signal's (a points wire also broken), and a list port's dot is a double ring.

- ``numbers`` joins ``numbers`` and ``vector`` (a vector is a list of fixed length: one of another
  length fails when it arrives) and an :ref:`elementwise <elementwise>` block's signal ports: that
  is the map. Range → ``function`` (``f = "x^2"``) → ``gain`` squares and scales every item. A list
  has one unit, every item's: a list in ``m`` into a port in ``rad`` is refused.
- ``points`` joins only ``points`` (and ``any``). On a points port ``dimension`` and ``components``
  describe each point, not the list: points of 2 coordinates into a port expecting 3 are refused,
  and coordinate units are checked by position, as a vector's are. A list's own length is never
  declared.
- Neither joins a plain ``signal`` port, and a number is not a list.

Blocks:

- ``range`` (→ ``y``, numbers): ``count`` numbers evenly spaced from ``start`` to ``stop``, both
  included (``0, 1, 11`` gives 0, 0.1 … 1).
- ``series`` (→ ``y``, numbers): ``count`` numbers from ``start``, each ``step`` more.
- ``list_length`` (``list`` → ``n``): how many items.
- ``item`` (``list``, ``i`` → ``y``): the item at ``index`` (0 the first, -1 the last), or at input
  ``i`` when it is wired; outside the list fails, naming the block.
- ``aggregate`` (``list`` → ``y``; ``op``: ``sum``, ``mean``, ``min``, ``max``): one number from the
  list. The sum of none is 0; the others of an empty list fail.
- ``points`` (one numbers input per name → ``points``): joins lists of coordinates into a list of
  points, as ``mux`` joins numbers. Param ``names`` (``"x z"``; default ``"x y"``) gives the inputs
  and the points' coordinates, ``units`` their units by position. Lists of different lengths fail;
  an unwired coordinate is 0.
- ``coordinates`` (``points`` → one numbers output per name): the other way, as ``demux``.

``list_length``, ``item`` and ``aggregate`` are magic blocks with two forms, picked by ``form``:
``numbers`` (the default) and ``points``, whose ``list`` is a list of points. In the points form
``item`` gives a point (a vector) and ``aggregate`` one value per coordinate: ``mean`` is the
centroid, ``min`` and ``max`` the corners of the bounding box.

On the Dashboard a ``display`` shows a list's first values and how many there are (a list of
points as text: ``5 points: (-1, 1), (-0.5, 0.25), …``); a ``graph`` in a run-once plots a list of
numbers over its index and a list of points as a curve, the second coordinate over the first.
Fan-out of a whole run over a list's items (a sweep) and a ``Collect`` of their results are not
part of this package yet.

.. _pkg-dashboard:

dashboard
---------

Monitors: ``graph`` (plots ``in`` over time; a vector in a clock run as one trace per
component, in a run-once by index; a list of points in a run-once as y over x), ``display`` (its
latest number, text, vector or list: a long list as its first values and its length) and
``scope`` (inputs ``a``, ``b``… by its ``inputs`` param, 2–26, each a channel, a vector one per
component; see :doc:`dashboard`).
Control: ``switch`` (output ``y``, 0 or 1), a Board switch the user flips during a run. Param
``mode`` is ``toggle`` (a click flips it) or ``momentary`` (on while held); ``initial`` is its
state at the start of a run; ``key`` also binds a keyboard key. A Monitor that declares outputs,
like ``switch``, runs as an ordinary block and is shown from its first output.
Indicator: ``lamp`` (``in`` → ``y``, 0 or 1), lit while ``in`` passes ``threshold`` by ``op``
(``>``, ``>=``, ``<``, ``<=``, ``==``, ``!=``; the default ``> 0.5`` lights on a boolean); with
``latch`` it stays on from its first trip until the run ends. ``label`` is its text on the
Dashboard and ``color`` its colour when lit: ``signal`` or ``danger``. Its output can end a run
(a Step flow's ``when`` → ``stop``).

.. _pkg-step:

step
----

A Step flow's path: ``start`` (a hat: once), ``when`` (a hat: each time signal ``cond`` rises),
``wait`` (``seconds``), ``repeat`` (leaves by ``loop`` ``times`` times, then ``done``), ``stop``
(ends the run). To branch, use a ``base`` logic block in its step form.

opencv (external toolkit)
-------------------------

Optional external toolkit from ``paperboat-toolkit-opencv`` (installed into ``engine/toolkit/opencv``;
``paperboat::toolkit_opencv``, ``register_opencv``). Everything takes ``image`` in and gives ``image``
out unless it says otherwise:

- **Geometry**: ``resize`` (``width``, ``height``, ``scale``, ``interpolation``), ``crop`` (``x``,
  ``y``, ``width``, ``height``), ``flip`` (``mode``), ``rotate`` (``angle``, ``scale``, ``expand``).
- **Colour and tone**: ``gray``, ``convert`` (``space``: gray, bgr, rgb, hsv, hls, lab),
  ``normalize`` (``alpha``, ``beta``, ``mode``), ``equalize``, ``clahe`` (``clip``, ``tiles``).
- **Filters and edges**: ``blur`` (``size``), ``median`` (``size``), ``threshold`` (``level``,
  ``otsu``, ``invert``), ``adaptive_threshold`` (``size``, ``c``, ``invert``), ``hsv_range``,
  ``canny`` (``low``, ``high``), ``sobel`` (``dx``, ``dy``, ``size``), ``laplacian`` (``size``),
  ``erode``, ``dilate``, ``open``, ``close`` (``size``, ``iterations``), ``invert``, ``distance``.
- **Image algebra**: ``bitwise`` (``op``; a grey ``image2`` is a mask), ``blend`` (``alpha``),
  ``absdiff``, ``background`` (``rate``, ``threshold``, ``open``; a motion mask and the learned
  background).
- **Analysis**: ``stats`` (mean, std, min, max, nonzero, pixels), ``centroid`` (the largest white
  blob), ``contours`` (count, largest centre/area/box, marked image), ``hough_lines``,
  ``hough_circles``, ``template_match`` (a ``template`` file).
- **Annotation and output**: ``draw`` (box, circle, cross or text), ``save`` (a ``file``, ``format``,
  ``quality``, ``once``).

Monitor ``view`` (an image instrument, from the toolkit's ``web/index.tsx``). Station object
``image`` (props ``file``, ``width``; linked block out ``image``). The toolkit also ships
``ocv-01``…``ocv-03`` examples under ``examples/``; ``paperboat toolkit install opencv`` copies them
to ``engine/toolkit/opencv/examples`` and the CLI lists them with ``paperboat backends``.

Its ``toolkits.toml`` entry lists these block types in ``blocks = [...]``; the engine sends them in
``ToolkitStatus.blocks`` so the UI can name the toolkit when a flow uses a block that is not installed.
A new optional toolkit does the same, with no change to the web app.

optimization (external toolkit)
-------------------------------

Optional external toolkit from ``paperboat-toolkit-optimization`` (installed into
``engine/toolkit/optimization``; ``paperboat::toolkit_optimization``, ``register_optimization``).
Step blocks from a JSON file, solved by OSQP: ``qp`` (min ½·xᵀPx + qᵀx), ``lp`` (min cᵀx)
and ``nlp`` (min f(x), Ipopt when built). A solve block has step ``in``/``out``: the path
arrives, one solve, the path leaves. Outputs ``x`` (numbers), ``objective``, ``iterations``
and ``status``. The file uses ``P``, ``q`` for a QP (``P``'s upper triangle), ``c`` for an LP,
or ``variables``/``objective``/``constraints`` for an NLP, with optional ``A``, ``l`` and ``u``;
a ``null`` bound is infinite.

.. _pkg-control:

control (external toolkit)
--------------------------

Optional external toolkit from ``paperboat-toolkit-control`` (installed into
``engine/toolkit/control``; ``paperboat::toolkit_control``, ``register_control``). It carries
the clock dynamics (all but ``delay`` :ref:`elementwise <elementwise>`): ``pid`` (``e`` → ``u``;
``kp``, ``ki``, ``kd``), ``integrator`` (``u`` → ``y``; ``initial``), ``lag`` (``u`` → ``y``;
``tau``; 0 is a one-step delay) and ``delay`` (``u`` → ``y``: its input ``steps`` clock steps
ago, ``initial`` before then; any value). The last three have no direct feedthrough (they
output their state, then take the input), so a feedback loop may pass through them:
integrator → gain → back into the integrator is x' = −k x. A loop without one is an algebraic
loop and fails. C++ helpers ``rk4_step`` and ``euler_step`` live in
``paperboat/toolkit/control/simulator.hpp``.

It also links the optimization toolkit's ``Workspace`` for per-step Signal blocks: ``mpc``
(a condensed QP from ``A``, ``B``, ``Q``, ``R``, ``N`` and ``u`` bounds), ``lp_step`` (an LP
every step) and ``nmpc`` (an NLP every step, needs Ipopt). They are ``feedthrough = false``:
``outputs()`` returns the last ``u``, ``step()`` solves for the next one. Outputs ``u``,
``status``, ``iterations`` and ``solve_time``. Install ``optimization`` with it.
