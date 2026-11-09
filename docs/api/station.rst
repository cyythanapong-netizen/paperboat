The Station
===========

Station objects
---------------

A station object is a thing the domain acts on. Its type (``ObjectDef``) declares its
**properties** (edited on the Station page only), its **link slots** (other objects it
holds, such as a spring's ``a`` and ``b``) and optionally a **linked block** (the block on
the Flow page through which a flow pushes on it and reads it). An instance owns its state:

- ``read()``: the linked block's outputs from the current state (no feedthrough),
- ``step(ctx, in)``: advance one clock step under the linked block's inputs,
- ``entities(id, z)``: its shapes on the scene,
- ``link(slot, other)``: another object fills one of its slots, once, before a run.

Define them in code (C++ :cpp:func:`paperboat::toolkit::define_object`, Python
:py:class:`paperboat.StationObject`); the descriptor's ``[station]`` holds the starter set.

Views
-----

``[station] view`` says how the Station page shows the station.

``scene`` (default)
   The built-in 2D or 3D scene of **entities**: ``box``, ``sphere`` (``size[0]`` = radius), ``cylinder`` (``size[0]`` = radius, ``size[1]`` = length along y),
   ``line`` (``pos`` → ``to``) and ``image`` (a picture ``size[0]`` × ``size[1]``), with
   ``pos``, ``size``, ``rot`` (radians, x, y, z order), ``scale``, ``color``. Station objects,
   the domain state and blocks draw them; ``scene.patch`` updates them during a run. The
   backend's ``view`` hint picks the camera (``{"mode": "2d", "center": [x, y], "height": h}``; in 3D,
   ``camera`` and ``floor``, the height of the floor grid; without it the grid sits under the
   lowest entity of the first scene, never above 0, so it doesn't cut through the model).

``graph``
   Station objects as nodes and their links as wires: drag from an object's right handle
   onto another's slot to link it; delete a wire to unlink. For structures built from
   parts, tested from the Flow (``paperboat-examples/chain-python``).

``web``
   The backend's own page, for a station the built-in views can't show (a map, a
   document, a game's own look). See below.

Web stations
------------

The page lives in the backend's folder (``page = "station/index.html"``). The engine serves
that folder over HTTP on the backend's own port, and the bridge script at
``/paperboat/station.js``; nothing else. The UI shows the page in a sandboxed iframe
(scripts only, no same-origin) and relays every protocol message to it. **The page renders;
the engine decides**: it may send back only what the Station could (a station edit, a key).

.. code-block:: html

   <canvas id="view"></canvas>
   <script src="/paperboat/station.js"></script>
   <script>
     paperboat.on("entities", (entities) => draw(entities));   // Map id → entity, kept current
     paperboat.on("run.finished", (m) => showStatus(m.status));
   </script>

.. js:function:: paperboat.on(type, fn)

   Call ``fn(message)`` for every protocol message of ``type`` (``"scene"``, ``"signal"``,
   ``"log"``, ``"run.started"``…), or ``fn(entities)`` for ``"entities"`` after each ``scene``
   and ``scene.patch``. Returns ``paperboat`` (chainable).

.. js:attribute:: paperboat.entities

   The scene's entities by id (a ``Map``), merged from ``scene`` and ``scene.patch``.

.. js:attribute:: paperboat.state

   ``{ view, station, catalog, running }``: the latest view hint, station, catalog, and
   whether a run is going.

.. js:function:: paperboat.setStation(station)

   Replace the station (``{ objects: [...] }``), as an edit on the Station panel does
   (``station.set``). The backend checks it.

.. js:function:: paperboat.input(key, down)

   A key going down or up (a ``KeyboardEvent.code``). Keys pressed while the page has focus
   are forwarded automatically, so Key blocks work.

When the page loads, the bridge posts ``station.ready`` and the UI replays ``hello``,
``catalog``, the current ``scene`` and ``station.load``. See ``paperboat-examples/runner-python/station``.
