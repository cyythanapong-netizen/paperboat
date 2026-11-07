Dashboard and Console
=====================

Instruments
-----------

A **monitor** block (kind ``monitor``) shows a value on the Dashboard; its definition's
``instrument`` says how: ``graph`` (a plot over time, or over list index in a run-once),
``display`` (the latest number or text), ``image`` (a picture) or ``scope`` (see below). The engine samples each
monitor once per pass and sends ``signal`` messages (``v`` is a number, or text for a
display or an image's data URL). A monitor that must turn its input into something showable
returns it as output ``value`` (``opencv/view`` turns an image into a data URL); otherwise its
input ``in`` is shown.

Define one in C++ with ``kind = Kind::Monitor`` and ``instrument``, or in Python by
subclassing :py:class:`paperboat.Monitor`.

Graph and Scope
~~~~~~~~~~~~~~~

``graph`` is the glance: one signal's shape and its latest number. A vector into it is one
trace per component in a clock run (by index in a run-once).

``scope`` (``dashboard/scope``, #37) is the instrument to study signals with. It has several
inputs (``a``, ``b``… set by its ``inputs`` param, 2 to 26); the engine sends one ``signal``
per wired input with ``port`` naming it, and a vector stays one sample with its
``components``, so **one vector wire (a Mux's output, a state) is one channel per
component**. The legend names each channel by the output wired in (``plant.θ``), with its
unit; each unit gets a y axis. It has three modes (``time``, ``xy``: the first channel
against the others, ``fft``: dB magnitude of the visible window, Hann, with each channel's
peak frequency), two cursors with Δt, 1/Δt and a Δ per channel, an edge trigger (source,
level, position in the window, auto or single), log y, four draw styles, and CSV/PNG export.
``+ tap`` wires any output in (to the next free input, adding one), as one undo step.

How a Scope draws is the Board layout's, not a block param, so it changes during a run and
saves with the project: an instrument's ``scope`` table (``mode``, ``window`` in seconds,
``ylog``, ``hidden`` channel ids, ``draw``, ``trigger`` = ``{ edge, source, level, position,
single }``). A backend may ship one:

.. code-block:: toml

   [dashboard]
   instruments = [
     { block = "scope1", size = "xlarge", scope = { window = 2, trigger = { source = "a", level = 0 } } },
   ]

Layout
------

The Dashboard lays itself out; there is no layout to edit. Readouts (displays) go in a
strip of small cards on top, then pictures, then plots, as equal tiles in a grid that fits
the width. Within a kind, instruments follow their monitor blocks on the Flow canvas, left
to right, then top to bottom: a pipeline's stages read in order, and moving a block moves
its instrument.

Decorations and tabs
--------------------

A backend's default layout (the descriptor's ``[dashboard]``) and the user's own arrangement
may add things that are not instruments: a **section** (a titled frame round a group of
cells), **separators** (``hline``, ``vline``), a **label** (a heading or a note) and the
**console** (the Console's lines in a frame), and **tabs**, pages with a grid each. Labels
and the console take cells; sections and separators are drawn in the gaps. Text in a label or
section is a formatted string: ``{speed}`` shows that monitor block's latest value, ``{speed:.2f}``
with that many decimals, ``{{`` and ``}}`` are braces. A name that is no monitor block but a
**variable** shows the variable's value (live: it changes as the variable is edited, no run
needed); if a block and a variable share a name, the block wins. In the UI, Arrange adds and
edits them; while editing text, click a value chip (or drag it in) to put ``{name}`` at the
caret; variables have their own chips. Renaming a variable rewrites ``{old}`` in labels and
section titles, deleting one replaces it with its value, and the variables sheet lists them
under "Used by".

The Console
-----------

``log`` messages (``info``, ``warn``, ``error``). A run logs what it does; when it fails, the
error names the block at fault (``node``). Blocks, steps and station objects write their own
lines with ``Context::log`` (C++) or ``ctx.log`` (Python); the line carries the block's id,
which the Console links to. The domain state's ``summary()`` is logged when a run ends,
followed by each station object's final readings.
