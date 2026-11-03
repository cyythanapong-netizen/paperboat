The Flow
========

Graphs
------

A graph is blocks (nodes) and wires (edges), sent with ``graph.run``. The backend checks it
against its catalog and station, makes an instance of each block, and runs it.

- **A wire joins ports of one type** (step to step, signal to signal, image to image). A
  signal input takes one wire; a step output leads to one block.
- **An unconnected input reads as 0** (absent from ``in``). A missing param takes its
  default.
- **Signal wires may not loop**, except through a linked block or a step block (both read
  their inputs after the pass). A cycle fails the run.
- **A run never crashes the backend.** A bad graph or a failing block fails the run with
  an error ``log`` and ``run.finished`` ``failed``.

Signal flow
-----------

Blocks run in dependency order. With the ``Clock`` run style they run every clock step
(``dt``) for ``duration`` seconds; after each pass station objects step under their linked
blocks' inputs, then the domain state steps (and may end the run). With ``Once`` they run a
single time, and wires may carry lists, tables, text or handles.

Step flow
---------

A graph with step ports is a Step flow. **Hats** (blocks with step outputs and no step
input: Start, When) start **paths**. A path is at one block at a time: the engine calls
the block's ``enter`` when the path arrives and ``tick`` each clock step until it names the
step output to leave by. Signal blocks still run each clock step first and feed
conditions. Several paths run side by side; a path reaching a block another path is in
waits; Stop ends the run. ``node.state`` reports ``running`` and ``done`` for each block a
path passes.

Groups
------

``graph.groups`` holds groups of blocks (a block is in at most one).

**Visual groups** tidy the canvas. The backend runs their blocks as if ungrouped, in the
sheet's flow type. Collapsed, the UI draws a group as one block whose ports are the wires
crossing its edge.

**Story groups** are steps. A story's id is a node with step ports ``in`` and ``next``, and
its blocks (signal blocks only) run only while a path is at it: once per clock step for
``params.seconds`` (``0``: once), then the path leaves by ``next``. What they computed stays
on their outputs, so the next story reads the results. With ``params.reset`` its blocks
are made afresh each time a path arrives, so a story in a loop starts over. A test
procedure reads as
``Start → Pull test (6 s) → Check (once)`` (``examples/backends/chain-python``); an
animation as ``Start → Walk a square → Spin (2 s) → Report (once)``
(``examples/backends/animation-python``, where Walk a square is a visual group of step
blocks and Spin a story around a signal block that turns the actor while it runs).

.. code-block:: json

   {"groups": [{"id": "pull test", "kind": "story", "nodes": ["pull", "x of mass-2"], "params": {"seconds": 6}},
               {"id": "check", "kind": "story", "nodes": ["limit", "overloaded"]}]}

Lost and failing blocks
-----------------------

A block whose type the backend doesn't offer (an old graph, a block renamed in code)
stays on the canvas as a red **missing** block, with its type, params and wires; a run
with it fails and names it. When a block fails a run (an unknown type, an error it
throws, a bad wire's end), the backend sends ``node.state`` ``failed`` for it and puts its
id in the error ``log`` (``node``); the UI marks it, and the Console line links to it.

The canvas survives a backend coming back: if the user changed the graph since the same
backend loaded it, the graph stays and is checked against the new catalog.
