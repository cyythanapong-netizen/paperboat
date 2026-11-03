Paperboat API
=============

Paperboat is a framework. Connect it to a **backend** (any program: a simulation, an image
pipeline, a game, a test bench) and it becomes an app for that backend's domain, with four
pages: the **Station** (the things the domain is about), the **Flow** (what is done to
them, as a graph of blocks), the **Dashboard** (results, as instruments) and the
**Console** (what happened).

The engine is C++. A backend is a small program on the engine's SDK, in C++ or in Python
through the bindings; it adds only what its domain has and the toolkit doesn't. This
reference is for developers writing backends.

.. toctree::
   :maxdepth: 2
   :caption: Guide

   concepts
   quickstart
   descriptor
   flow
   station
   dashboard
   testing

.. toctree::
   :maxdepth: 2
   :caption: Reference

   cpp
   python
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
   examples/backends/   example backends, each a project of its own
   GUIDELINES.md        rules and layout; TODO.md: open work
