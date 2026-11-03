Testing a backend
=================

``--run``
---------

Every backend has the engine's command line: ``--port N`` (serve there) and ``--run`` (run
the starter graph on the starter station once, as fast as it can, print every protocol
message as one JSON line, and exit 0 when the run is done, 1 when it failed). C++:
:cpp:func:`paperboat::core::main`; Python: :py:func:`paperboat.serve`.

Golden results
--------------

``expected.json`` next to a backend's descriptor says what its starter run must produce;
``protocol/golden.py`` (``npm run golden``) runs every example with ``--run`` and compares.

.. code-block:: json

   {
     "run": "build/paperboat-parts",
     "status": "done",
     "signals": {"total": 16, "graph-theta": [3.780e-07, 1e-10]},
     "logs": ["evaluated 14 blocks"]
   }

``run`` is the executable (C++) or ``server.py`` (Python); ``signals`` maps a monitor to its
last number, exact or ``[value, tolerance]``; ``logs`` lists text some log line must contain.
The examples exist to prove the framework: a framework change that alters any example's
result fails here.

Protocol conformance
--------------------

``protocol/smoke_test.py --port N`` (``npm run smoke -- N``) checks any running backend
without knowing its domain: the handshake, the starter run, a cycle (must fail), a block
the backend doesn't have (must fail and be named), a cancel. Every message either way is
checked against the JSON Schema (``protocol/schema/paperboat-0.schema.json``).

Unit tests
----------

Test a domain's blocks and station objects on their own: make one, call ``step`` with a
``Context``, check the outputs (``engine/tests`` has many). A C++ test can run a whole graph
with :cpp:func:`paperboat::core::run` and ``real_time = false``, collecting the messages.
