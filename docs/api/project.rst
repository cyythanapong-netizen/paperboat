Projects
========

A **project** is everything a user made, saved as one file: the Flow, the Station, the
variables, the Dashboard arrangement and the run settings. It belongs to the user, not to the
backend: a backend ships a *starter* (its descriptor's ``[graph]``, ``[[variables]]`` and
``[dashboard]``), and a project is what the user did with it.

A backend needs no code for projects. The app builds and reads the file itself, from shapes the
protocol already has.

The file
--------

``name.ppj``: readable JSON, indented, so a diff shows what changed. Parts that are
empty are left out. Files saved before the extension changed (``name.paperboat.json``) still
open.

.. code-block:: json

   {
     "paperboat": 0,
     "backend": "cart-pole",
     "flow": {
       "nodes": [{ "id": "pid-1", "type": "pid", "params": { "kp": 40 }, "pos": [260, 0] }],
       "edges": [{ "from": ["angle-1", "angle"], "to": ["pid-1", "error"] }]
     },
     "station": { "objects": [{ "id": "cart-1", "type": "cartpole", "props": { "pole_length": 1.2 } }] },
     "variables": [{ "name": "pole_length", "type": "number", "value": 1.2, "unit": "m" }],
     "dashboard": { "instruments": [{ "block": "graph-1", "size": "large", "at": [1, 0] }] },
     "run": { "duration": 5 }
   }

.. list-table::
   :header-rows: 1
   :widths: 14 86

   * - Field
     - Meaning
   * - ``paperboat``
     - The project format version, ``0`` today. A newer file is refused, and the message says
       which version it is.
   * - ``backend``
     - The ``engine`` the project was made on (``hello``). Opening it on another backend warns
       first.
   * - ``flow``
     - The Flow canvas as a protocol ``Graph``, groups included. Every node has its place
       (``pos``), and a wire the user moved has its bends (``points``); backends ignore both.
   * - ``station``
     - The Station, a protocol ``Station``.
   * - ``variables``
     - The list the Variables sheet shows (the backend's defaults and the user's own).
   * - ``dashboard``
     - The user's Dashboard arrangement, if they made one (a protocol ``DashboardLayout``).
   * - ``run``
     - Only the run settings the user changed from the backend's defaults.

The shape is ``Project`` in ``protocol/schema/paperboat-0.schema.json`` (and ``protocol.ts``,
generated from it). It is not a message: the app writes and reads it.

In the app
----------

The **File** menu: **Open…** (Ctrl+O), **Save** (Ctrl+S), **Save as…** (Ctrl+Shift+S),
**Restore last session** and **Recent projects**. Dropping a project file on the window opens
it. The bar shows the project's name, with a dot while there are changes not saved.

* Where the browser has the File System Access API (Chromium), Save writes back to the file the
  user chose. Elsewhere Save downloads a copy and Open uses a file input.
* Opening checks the file first and changes nothing if it can't be read: not JSON, not a
  project, a newer format, or damaged (the message names where). It asks first when the
  project comes from another backend or replaces unsaved changes. Edit > **Undo open project**
  brings back everything that was open.
* A block whose type the opening backend lacks stays on the canvas as a lost block, with its
  params and wires, so opening a project on a backend with fewer blocks loses nothing.
* The browser keeps unsaved work as it happens, per backend, and offers it back after a reload
  (**Restore your last session?**). It also keeps the last few saved or opened projects, so
  Recent projects can reopen them.

From the CLI
------------

You can launch a saved project with the CLI:

.. code-block:: bash

   paperboat open project.ppj

This validates the project format, resolves and starts the matching backend, ensures the Web UI is
running, and loads the project. To execute the saved project headlessly:

.. code-block:: bash

   paperboat open project.ppj --run

A project holds what the user made, not what a run did. A run's results are kept separately, as
a **run record**.

Run records
-----------

``paperboat run --record <dir>`` (and ``paperboat open project.ppj --record <dir>``, with or
without ``--run``) keeps the run as ``<dir>/record.json``, plus nothing else: plain JSON,
indented, so two runs of the same project diff to the settings, variables and samples that
changed.

.. code-block:: bash

   paperboat run 02-cart-pole-cpp --record runs/pole-1

.. code-block:: json

   {
     "paperboat": 0,
     "run": { "id": "run-1", "status": "done", "started": "2026-11-10T14:48:54Z", "finished": "2026-11-10T14:49:04Z" },
     "backend": { "name": "cart-pole", "version": "0.2.0" },
     "tool_versions": { "paperboat": "0.1.0-alpha.3" },
     "command": "paperboat run 02-cart-pole-cpp --record runs/pole-1",
     "flow": { "nodes": [], "edges": [] },
     "station": { "objects": [] },
     "variables": [],
     "settings": { "duration": 10, "dt": 0.01 },
     "signals": [
       { "type": "signal", "node": "angle", "t": 0, "v": 0.2 },
       { "type": "signal", "node": "angle", "t": 0.01, "v": 0.199 }
     ],
     "scene": {
       "base": { "cart-1/body": { "id": "cart-1/body", "shape": "box", "pos": [0, 0, 0] } },
       "frames": [{ "t": 0.01, "entities": [{ "id": "cart-1/body", "pos": [0.01, 0, 0] }] }]
     }
   }

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - Field
     - Meaning
   * - ``paperboat``
     - The record format version, ``0`` today.
   * - ``run``
     - Its id (``run-1``), status (``done``, ``failed``, ``cancelled``), and wall-clock start and finish.
   * - ``backend``
     - The ``hello.engine`` name and, when ``backend.toml`` has one, its version.
   * - ``tool_versions``
     - The versions that made the record (paperboat, and the backend).
   * - ``command``
     - The command line that asked for the run.
   * - ``flow``, ``station``, ``variables``, ``settings``
     - The ``graph.run`` request the run was given, and the settings it resolved to.
   * - ``signals``
     - Every ``signal`` sample the monitors sent, in arrival order: what the Board scopes saw.
   * - ``scene``
     - The Station's opening ``base``, then each ``scene.patch`` in ``frames``, on the run clock.
       A run that never moved the Station has none.
   * - ``toolkits``
     - The optional toolkits the backend offered, when it had any.

The shape is ``protocol/schema/record-0.schema.json``. A directory that cannot be made or a file
that cannot be written fails with a named error (``cannot create record directory …`` or
``cannot write run record …``); it never stops the run.

In the app, **File > Open run record…** opens a saved record for replay (#284), and
**File > Save run record…** writes one from the run the app has in memory.
