Toolkit packages
================

Built-in templates, by package (``engine/toolkit``). Import a package in a descriptor
(``import = ["signal", "math"]``) or use one template (``use = "math/gain"``). Paperboat is
not Simulink: ``base`` holds only what any domain needs; everything else lives in the
package of what it is about. List them with ``paperboat-catalog --templates``.

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

Pointwise arithmetic on signals: ``gain`` (``u`` → ``y``; ``k``), ``sum`` (``a``, ``b`` → ``y``),
``threshold`` (``u`` → ``y``: 1 above ``level``, else 0), ``saturation`` (``u`` → ``y``; ``min``,
``max``).

dashboard
---------

Monitors: ``graph`` (plots ``in`` over time) and ``display`` (its latest number or text).

control
-------

Dynamics on a clock: ``pid`` (``e`` → ``u``; ``kp``, ``ki``, ``kd``), ``integrator`` (``u`` → ``y``;
``initial``), ``lag`` (``u`` → ``y``; ``tau``). Station object ``cart-pole`` (props ``cart_mass``,
``pole_mass``, ``pole_length``, ``max_force``, ``x0``, ``theta0``; linked block: in ``force``, out
``x``, ``v``, ``theta``, ``omega``). C++ helpers ``rk4_step`` and ``euler_step`` in
``paperboat/toolkit/control/simulator.hpp``.

step
----

A Step flow's path: ``start`` (a hat: once), ``when`` (a hat: each time signal ``cond`` rises),
``wait`` (``seconds``), ``repeat`` (leaves by ``loop`` ``times`` times, then ``done``), ``stop``
(ends the run). To branch, use a ``base`` logic block in its step form.

opencv
------

Optional (built when OpenCV is found; ``paperboat::toolkit_opencv``,
``register_opencv``). Image filters, ``image`` → ``image``: ``gray``, ``blur`` (``size``),
``median`` (``size``), ``threshold`` (``level``, ``otsu``, ``invert``), ``adaptive_threshold``
(``size``, ``c``, ``invert``), ``hsv_range``, ``canny`` (``low``, ``high``), ``erode``, ``dilate``,
``open``, ``close`` (``size``, ``iterations``), ``invert``, ``distance``. Monitor ``view`` (an image
instrument). Station object ``image`` (props ``file``, ``width``; linked block out ``image``).
