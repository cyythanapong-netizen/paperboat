Protocol
========

One WebSocket between the UI and a backend; every message is a JSON object with a
``type``. The source of truth for shapes is the JSON Schema
``protocol/schema/paperboat-0.schema.json`` (``$defs/toUI``, ``$defs/toBackend``);
``protocol/README.md`` explains each message, and the web app's types
(``apps/web/src/protocol.ts``) are generated from the schema (``npm run gen:protocol``). A
backend on the engine speaks it already.

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
``signal``         ``node``, ``t``, ``v`` (number, or text). A monitor's sample
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
