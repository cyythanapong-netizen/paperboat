Dashboard and Console
=====================

Instruments
-----------

A **monitor** block (kind ``monitor``) shows a value on the Dashboard; its definition's
``instrument`` says how: ``graph`` (a plot over time, or over list index in a run-once),
``display`` (the latest number or text) or ``image`` (a picture). The engine samples each
monitor once per pass and sends ``signal`` messages (``v`` is a number, or text for a
display or an image's data URL). A monitor that must turn its input into something showable
returns it as output ``value`` (``opencv/view`` turns an image into a data URL); otherwise its
input ``in`` is shown.

Define one in C++ with ``kind = Kind::Monitor`` and ``instrument``, or in Python by
subclassing :py:class:`paperboat.Monitor`.

Layout
------

Without a layout it lays itself out. Readouts (displays) go in a
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

Controls
--------

A variable marked ``exposed`` gets a control (a slider, checkbox, dropdown or text field) in the
**Controls** tile, a ``controls`` decoration the layout places like an instrument (default
4×3). A ``tunable`` one can be dragged during a run and takes effect between two steps;
others apply from the next run. A variable marked ``dock`` has its control in the run dock
too. See :doc:`descriptor` for the flags.

The Console
-----------

``log`` messages (``info``, ``warn``, ``error``). A run logs what it does; when it fails, the
error names the block at fault (``node``). Blocks, steps and station objects write their own
lines with ``Context::log`` (C++) or ``ctx.log`` (Python); the line carries the block's id,
which the Console links to. The domain state's ``summary()`` is logged when a run ends,
followed by each station object's final readings.
