// Paperboat web station bridge, served by the engine at /paperboat/station.js.
//
// A web station is a backend's own page shown on the Station page (in a sandboxed iframe)
// instead of the built-in scene. The page only renders and reports what the user does; the
// engine stays authoritative. Load this script, then:
//
//   paperboat.on("entities", (entities) => draw(entities));   // Map id → entity, kept current
//   paperboat.on("signal", (msg) => …);                        // any protocol message type
//   paperboat.setStation(station);                             // like an edit on the Station panel
//
// Keys pressed while the page has focus are forwarded as `input`, so Key blocks still work.
(function () {
  "use strict";
  const handlers = new Map(); // message type → [fn]
  const entities = new Map(); // id → entity, from `scene` and `scene.patch`
  const state = { view: null, station: null, catalog: null, running: false };

  function emit(type, payload) {
    for (const fn of handlers.get(type) || []) {
      try {
        fn(payload);
      } catch (err) {
        console.error(err);
      }
    }
  }

  function receive(msg) {
    if (msg.type === "scene") {
      entities.clear();
      for (const e of msg.entities) entities.set(e.id, e);
      if (msg.view) state.view = msg.view;
    } else if (msg.type === "scene.patch") {
      for (const e of msg.entities) entities.set(e.id, { ...entities.get(e.id), ...e });
    } else if (msg.type === "station.load") {
      state.station = msg.station;
    } else if (msg.type === "catalog") {
      state.catalog = msg;
    } else if (msg.type === "run.started") {
      state.running = true;
    } else if (msg.type === "run.finished") {
      state.running = false;
    }
    emit(msg.type, msg);
    if (msg.type === "scene" || msg.type === "scene.patch") emit("entities", entities);
  }

  window.addEventListener("message", (event) => {
    if (event.source !== window.parent) return;
    const data = event.data;
    if (data && data.paperboat === 0 && data.message) receive(data.message);
  });

  function send(message) {
    window.parent.postMessage({ paperboat: 0, message }, "*");
  }

  for (const [type, down] of [["keydown", true], ["keyup", false]]) {
    window.addEventListener(type, (event) => {
      if (event.repeat) return;
      send({ type: "input", key: event.code, down });
      if (event.code === "Space" || event.code.startsWith("Arrow")) event.preventDefault();
    });
  }

  window.paperboat = {
    /** Call fn for each message of `type` ("scene", "signal", "run.finished"…), or "entities". */
    on(type, fn) {
      if (!handlers.has(type)) handlers.set(type, []);
      handlers.get(type).push(fn);
      return this;
    },
    /** The scene's entities by id, merged from scene and scene.patch. */
    entities,
    /** The latest view hint, station, catalog and whether a run is going. */
    state,
    /** Replace the station (objects and their props), as the Station panel does. */
    setStation(station) {
      send({ type: "station.set", station });
    },
    /** A key going down or up, as if pressed on the page. */
    input(key, down) {
      send({ type: "input", key, down });
    },
  };

  send({ type: "station.ready" }); // the host replays the latest scene, station and catalog
})();
