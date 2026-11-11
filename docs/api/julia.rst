Julia backends
==============

A backend's domain code can be Julia. The engine (C++) still loads the descriptor, checks
graphs, runs the toolkit's blocks and serves the protocol; `Paperboat.jl` only answers the
engine's events, so a Julia backend has no second graph runner, solver or renderer.

How it fits
-----------

`libpaperboat_host` is the engine as a shared library with a small C ABI. The Julia process
builds the domain's registry from JSON descriptions, starts the server, and then pumps:

1. the engine's run thread raises an event (``new``, ``step``, ``entities``, …);
2. ``pb_host_next`` hands it to the Julia thread;
3. Julia computes the answer and calls ``pb_host_reply``;
4. the run continues.

Every call into Julia happens on the Julia thread, so the runtime is never entered from an
engine thread. The engine object, catalog and server live in C++; nothing is reimplemented.

Quickstart
----------

.. code-block:: sh

   paperboat new demo --julia        # descriptor.toml, server.jl, domain/blocks.jl
   cd demo
   paperboat deps install            # .julia + Manifest.toml, isolated from ~/.julia
   paperboat run                     # serve ws://localhost:8790
   paperboat check                   # manifest, catalog, protocol smoke test, golden run

``paperboat build`` is not needed: there is nothing to compile. ``paperboat run --run`` runs
the starter graph once, headless, and prints protocol messages as JSON lines.

A block
-------

.. code-block:: julia

   struct Scale <: Paperboat.Block
       gain::Float64
   end

   Scale(; gain = 1.0) = Scale(gain)

   Paperboat.definition(::Type{Scale}) = Paperboat.block(
       "scale";
       label = "Scale",
       inputs = [Paperboat.port("u")],
       outputs = [Paperboat.port("y")],
       params = [Paperboat.param("gain", 1.0)],
   )

   Paperboat.step(block::Scale, ctx::Paperboat.Context, inputs) =
       Dict("y" => block.gain * get(inputs, "u", 0.0))

``definition`` is read once for the catalog; the block type's constructor receives its params
as keywords. ``step`` returns a dictionary of outputs by port id, and may call
``Paperboat.log(ctx, "text")`` to write a Console line.

The types
---------

``Paperboat.Block``
   ``step(block, ctx, inputs)``. A source is a ``Generator``: ``value(source, t)`` returns its
   output. ``ctx`` carries ``t``, ``dt`` and the instance ``id``.

``Paperboat.Step``
   A block on a Step flow's path: ``enter(step, ctx, inputs)``, then ``tick(step, ctx, inputs)``
   returns the step output to leave by (``""`` stays another clock step). ``may_start``,
   ``ends_run``, ``restarts`` and ``shared`` map to their engine counterparts.

``Paperboat.StationObject``
   ``read(object)`` (the linked block's outputs), ``step(object, ctx, inputs)``,
   ``entities(object, id)`` (shapes to draw) and ``link(object, slot)``.

``Paperboat.Model``
   A station object advanced by a solver: ``initial(model)`` and
   ``derivatives(model, t, x, inputs)``. The solver is the prop or the ``model`` definition's
   ``solver`` (``rk4``, ``rk45``, ``cvode``, ``ida``, ``discrete``).

Events
------

The engine asks, Julia answers. The host loop maps them to the methods above:

============================  ==========================================
event                         answer
============================  ==========================================
``new``                       construct the instance (``params``)
``step``                      ``{"outputs": {...}}``
``value``                     ``{"value": number}`` (a generator)
``enter`` / ``tick``          a Step's arrival and its port to leave by
``read`` / ``entities``       a station object's outputs and shapes
``derivatives``               ``{"dx": [...]}``
``state_entities`` / …        a domain state, when the backend declares one
============================  ==========================================

Values travel as JSON: numbers, text, lists of numbers, and tables (lists of rows). Opaque
handles (images, meshes) are C++-only for now.

Host library
------------

``Paperboat.host_library()`` uses ``PAPERBOAT_HOST_LIB`` when set, else the checkout's
``engine/build/host/libpaperboat_host.<so|dylib|dll>``. Build it with:

.. code-block:: sh

   cmake --build engine/build --target paperboat_host

A packaged backend ships ``Project.toml`` and ``Manifest.toml``; the environment itself
(``.julia/``) is not packed, the same rule as Python's ``.venv/``.
