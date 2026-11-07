Command line
============

``paperboat`` is one tool for running, building, checking and packaging backends and for
starting the web app. ``paperboat help`` lists everything; this page groups it.

With no command, on a terminal, it opens the console (:doc:`quickstart`); ``paperboat repl``
is the plain prompt. Every command below works the same in the console, in a shell and in a
script.

Run and manage backends
-----------------------

.. code-block:: sh

   paperboat run [path | pkg.ppk] [--port N] [--no-build] [--clean] [--time] [--verbose] [--var name=value]
   paperboat build [path] [--clean]      # compile a C++ backend only
   paperboat backends [--json]           # what you can run: examples and installed
   paperboat list [--json]               # running backends (alias: ps)
   paperboat restart [path | port]
   paperboat stop [path | port | --all]

``run`` (alias ``start``) builds a C++ backend, or sets up a Python one, and serves it in the
foreground. A path to a backend folder or a ``.ppk`` package works (:doc:`backend_layout`);
a bare name picks an example or an installed backend. ``--var`` sets a default variable for
the session (repeatable).

A backend's own binary takes a few flags of its own, handy in scripts and tests:

.. list-table::
   :widths: 24 76

   * - ``--port N``
     - Listen on this port.
   * - ``--run``
     - Run the starter once, headless; protocol messages go to stdout as JSON lines.
   * - ``--duration S``
     - With ``--run``: override the run's duration. ``0`` runs until stopped.
   * - ``--var name=value``
     - Set a variable for the run (:doc:`descriptor`).

Web app and examples
--------------------

.. code-block:: sh

   paperboat app start [--port N] [--host H] [--daemon]   # default port 3047
   paperboat app stop | restart | build
   paperboat examples start | stop | status               # every example backend, ports 8765-8773

Toolkits
--------

.. code-block:: sh

   paperboat toolkit list [--json]
   paperboat toolkit install <name> | uninstall <name> | versions <name>
   paperboat toolkit update <name> | --all

Check and package
-----------------

.. code-block:: sh

   paperboat check [path] [--fast] [--json | --json-stream]
   paperboat pack [path] [--out pkg.ppk] [--json-stream]
   paperboat unpack pkg.ppk [--dir dir] [--json-stream]

See :doc:`backend_layout` for the stages and the package format. ``--json-stream`` prints one
JSON event per line as work goes, then a ``result`` event.

Long runs
---------

A run whose duration is ``0`` (or empty in the run dock) goes on until **Cancel**, a Stop
block or the end of the headless run. When the computation falls behind real time, the
Console warns once ``computation delay: behind real-time by N s`` (when the lag passes 0.2 s,
then at most every 2 s) and the run dock shows an amber delay badge.
