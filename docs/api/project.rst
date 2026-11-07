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
       (``pos``), which backends ignore.
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
  project comes from another backend or replaces unsaved changes. A toast then offers **Undo**,
  which brings back everything that was open.
* A block whose type the opening backend lacks stays on the canvas as a lost block, with its
  params and wires, so opening a project on a backend with fewer blocks loses nothing.
* The browser keeps unsaved work as it happens, per backend, and offers it back after a reload
  (**Restore your last session?**). It also keeps the last few saved or opened projects, so
  Recent projects can reopen them.

What isn't in a project yet: the last run's results (logs, signals).
