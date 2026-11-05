Toolkit packages
================

Built-in templates, by package (``engine/toolkit``). Import a package in a descriptor
(``import = ["signal", "math"]``) or use one template (``use = "math/gain"``). Paperboat is
not Simulink: ``base`` holds only what any domain needs; everything else lives in the
package of what it is about. List them with ``paperboat-catalog --templates``.

Package structure
-----------------

Built-in packages live in ``engine/toolkit/src/<package>/``. Each folder has a TOML
``toolkit.toml`` manifest naming its C++ namespace and registrar functions::

   name = "math"
   register = ["add_arithmetic"]

CMake discovers the manifests and source files and generates the package-registration
dispatcher. A package's block implementation, ``BlockDef`` (including ``description`` and
``view``), and registration function stay together. A new registration function is listed
in the manifest; an additional block in an existing function needs no central registry or
description-map edit. ``signal`` also uses ``definitions = "blocks.toml"`` to embed its
definitions on the C++ ``signal/generator`` parent.

Optional toolkits with external dependencies live in ``engine/toolkit/<package>/`` with a
``toolkit.toml`` and a package-local ``CMakeLists.txt``. The engine discovers the folder;
the package's CMake file decides whether its dependency is available. The backend links and
registers an optional package explicitly.

base
----

Values of each datatype: ``constant`` (Number: a float, param ``value``), ``text`` (Text: a
string) and ``boolean`` (Boolean: a checkbox, 1 or 0 on the wire); all output ``y``. And
``key`` (out ``pressed``: 1 while its ``key`` param's key is held in the UI).

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
     - ``a``, ``b`` → ``y``: 1 or 0
     - ``in``, ``a``, ``b`` → ``yes``, ``no``
   * - ``not``
     - ``u`` → ``y``
     - ``in``, ``u`` → ``yes``, ``no``
   * - ``compare``
     - ``a``, ``b`` → ``y``; ``op``: > >= < <= == !=
     - ``in``, ``a``, ``b`` → ``yes``, ``no``

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
``function`` (f(t): its ``f`` is a text param, typed on the block, over ``t``, ``a``, ``b``,
``c``). A descriptor defines more the same way. Expressions: numbers, names, ``+ - * / % ^``,
comparisons (1 or 0), ``&& || !``, ``c ? a : b``, ``pi``, ``e`` and ``sin cos tan asin acos atan
atan2 sinh cosh tanh exp log log10 sqrt abs floor ceil round sign frac min max clamp mod
pow``.

math
----

Pointwise arithmetic on signals: ``gain`` (``u`` → ``y``; ``k``), ``sum`` (``a``, ``b``… → ``y``; its ``signs`` param, ``"++"`` by default, gives one input per sign and whether it adds or subtracts: ``"+-+"`` is ``a − b + c``),
``threshold`` (``u`` → ``y``: 1 above ``level``, else 0), ``saturation`` (``u`` → ``y``; ``min``,
``max``).

dashboard
---------

Monitors: ``graph`` (plots ``in`` over time) and ``display`` (its latest number or text).

control
-------

Dynamics on a clock: ``pid`` (``e`` → ``u``; ``kp``, ``ki``, ``kd``), ``integrator`` (``u`` → ``y``;
``initial``), ``lag`` (``u`` → ``y``; ``tau``; 0 is a one-step delay) and ``delay`` (``u`` → ``y``:
its input ``steps`` clock steps ago, ``initial`` before then; any value). The last three have
no direct feedthrough (they output their state, then take the input), so a feedback loop
may pass through them: integrator → gain → back into the integrator is x' = −k x. A loop
without one is an algebraic loop and fails. C++ helpers ``rk4_step`` and ``euler_step`` in
``paperboat/toolkit/control/simulator.hpp``.

step
----

A Step flow's path: ``start`` (a hat: once), ``when`` (a hat: each time signal ``cond`` rises),
``wait`` (``seconds``), ``repeat`` (leaves by ``loop`` ``times`` times, then ``done``), ``stop``
(ends the run). To branch, use a ``base`` logic block in its step form.

opencv (optional, external)
---------------------------

Image processing toolkit, shipped separately as ``paperboat-toolkit-opencv``
(https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv). Built against OpenCV;
register with ``paperboat::toolkit::opencv::register_opencv``. Image filters, ``image`` →
``image``: ``gray``, ``blur`` (``size``), ``median`` (``size``), ``threshold`` (``level``,
``otsu``, ``invert``), ``adaptive_threshold`` (``size``, ``c``, ``invert``), ``hsv_range``,
``canny`` (``low``, ``high``), ``erode``, ``dilate``, ``open``, ``close`` (``size``,
``iterations``), ``invert``, ``distance``. Monitor ``view`` (an image instrument). Station
object ``image`` (props ``file``, ``width``; linked block out ``image``).
