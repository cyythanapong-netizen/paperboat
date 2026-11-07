Protocol
========

One WebSocket between the UI and a backend; every message is a JSON object with a
``type``. The source of truth for shapes is the JSON Schema
``protocol/schema/paperboat-0.schema.json`` (``$defs/toUI``, ``$defs/toBackend``);
``protocol/README.md`` explains each message, and the web app's types
(``apps/web/src/protocol.ts``) are generated from the schema (``npm run gen:protocol``). A
backend on the engine speaks it already.

Message Quick Reference
-------------------------

.. list-table::
   :header-rows: 1
   :widths: 24 26 50

   * - Category
     - Message Type
     - Role & Payload
   * - **Handshake & Starters**
     - ``hello``
     - Engine metadata, protocol version, and station view hints on connect.
   * -
     - ``catalog``
     - Declarations of available blocks and station object templates.
   * -
     - ``graph.load``
     - Starter flow graph topology (nodes, edges, groups).
   * -
     - ``station.load``
     - Starter station equipment list and physical properties.
   * -
     - ``scene``
     - Complete visual geometry entities and initial camera view.
   * - **Live Telemetry & State**
     - ``run.started``
     - Confirmation of run launch with unique execution identifier.
   * -
     - ``signal``
     - Streaming telemetry sample emitted by a Monitor block (``t``, ``v``).
   * -
     - ``scene.patch``
     - Lightweight differential entity updates (positions, rotations, colors).
   * -
     - ``node.state``
     - Step flow lifecycle transitions (``running``, ``done``, ``failed``).
   * -
     - ``run.progress``
     - Fractional execution progress (0.0 to 1.0) towards duration.
   * -
     - ``log``
     - Diagnostic console messages (``info``, ``warn``, ``error``) tagged with node id.
   * -
     - ``run.finished``
     - Terminal completion status (``done``, ``failed``, ``cancelled``).
   * - **Client Commands**
     - ``graph.run``
     - Execute the flow with current graph topology and station overrides.
   * -
     - ``station.set``
     - Update physical object property values edited on the Station page.
   * -
     - ``run.cancel``
     - Abort the running simulation loop immediately.
   * -
     - ``input``
     - Forward key and dashboard-control down/up events to ``Key`` and ``Switch`` blocks.
       A press shows for at least one pass, even if it is released before the pass runs.

Backend → UI
------------

=================  =====================================================================
``hello``          ``protocol``, ``engine``, ``station: {view, page?}``. On connect
``catalog``        ``blocks``, ``objects?``. On connect
``scene``          ``entities``, ``view?``. On connect, after a station edit, after a run-once
``graph.load``     ``graph``: the starter. On connect
``station.load``   ``station``: the starter station. On connect, if there is one
``scene.patch``    ``entities`` (changed fields). During a run
``run.started``    ``run``
``run.progress``   ``run``, ``fraction`` (0–1)
``signal``         ``node``, ``t``, ``v`` (number, text or vector), ``components?``, ``port?`` (a Scope's input). A monitor's sample
``node.state``     ``node``, ``state``: ``running``, ``done``, ``failed``
``log``            ``level``, ``text``, ``node?``
``run.finished``   ``run``, ``status``: ``done``, ``failed``, ``cancelled``
=================  =====================================================================

UI → Backend
------------

=================  =====================================================================
``graph.run``      ``graph`` (``nodes``, ``edges``, ``groups?``), ``station?``
``station.set``    ``station``: an edit on the Station page; answered with ``scene``
``run.cancel``     —
``input``          ``key`` (a ``KeyboardEvent.code``), ``down``
=================  =====================================================================

Over HTTP, on the same port, a backend with a web station serves its page's folder and
``/paperboat/station.js`` (:doc:`station`).
