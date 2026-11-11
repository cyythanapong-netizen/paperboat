Paperboat Documentation
=======================

Paperboat is an interactive framework for engineering systems, simulations, and data
pipelines. Connect it to a **backend** (in C++, Python or Julia) and it becomes an application for
that backend's domain across four pages: the **Station** (physical scene & equipment), the
**Flow** (computation & signal graph), the **Board** (live instruments & telemetry), and the
**Console** (event logs & diagnostics).

The engine is C++. A backend is a small program on the engine's SDK, in C++, in Python
through the bindings, or in Julia through `Paperboat.jl`; it adds only what its domain has
and the toolkit doesn't.

.. toctree::
   :maxdepth: 2
   :caption: Core Manuals & Tutorials

   user_manual
   technical_manual
   tutorials

.. toctree::
   :maxdepth: 2
   :caption: Topic Guides

   concepts
   quickstart
   descriptor
   backend_layout
   flow
   station
   dashboard
   project
   testing

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   cpp
   python
   julia
   toolkit
   protocol

Where things are
----------------

.. code-block:: text

   engine/              the C++ engine: toolkit (blocks, station objects, descriptors),
                        core (graphs, runs, sessions, the server), tools, tests
   engine/core/include/paperboat/sdk.hpp
                        the one header a C++ backend includes
   python/              the Python bindings (paperboat._engine) and the paperboat package
   protocol/            the WebSocket protocol: README, JSON Schema, smoke and golden tests
   apps/web/            the UI (React): domain-free, built from what a backend sends
   ../paperboat-examples/  example backends (own repository), each a project of its own
   GUIDELINES.md        rules and layout; open work is GitHub issues
