import assert from "node:assert/strict";
import { test } from "node:test";
import { reduce, initial, type Action, type EngineState } from "../src/engine.ts";
import { ReplayStore } from "../src/replay.ts";
import { TraceStore } from "../src/traces.ts";
import type { ServerMsg } from "../src/protocol.ts";

const fresh = (): EngineState => ({ ...initial, traces: new TraceStore(), sceneHistory: new ReplayStore(), logs: [] });
/** `useEngine` feeds the trace store as messages arrive; the reducer alone only reads it. */
const feed = (s: EngineState, m: ServerMsg | Action) => {
  if (m.type === "signal") s.traces.add(m);
  else if (m.type === "signals") for (const one of m.list) s.traces.add(one);
  return reduce(s, m);
};
const play = (messages: (ServerMsg | Action)[], from: EngineState = fresh()) => messages.reduce(feed, from);

test("replay starts at the run's end, scrubs, steps and stops at the end (#284)", () => {
  const run = play([
    { type: "run.started", run: "r1" },
    { type: "signal", node: "sc", port: "a", t: 0, v: 1 },
    { type: "signal", node: "sc", port: "a", t: 0.5, v: 2 },
    { type: "signal", node: "sc", port: "a", t: 1, v: 3 },
    { type: "run.finished", run: "r1", status: "done" },
  ]);
  assert.equal(run.replay, null); // a finished run does not open a replay by itself

  const started = reduce(run, { type: "replay.start" });
  assert.deepEqual(started.replay, { active: true, playing: false, time: 1, from: 0, to: 1, speed: 1 });

  const sought = reduce(started, { type: "replay.seek", t: 0.4 });
  assert.equal(sought.replay?.time, 0.4);
  const stepped = reduce(sought, { type: "replay.step", direction: -1 });
  assert.equal(stepped.replay?.time, 0); // the run's sample spacing is 0.5 s
  const forward = reduce(stepped, { type: "replay.step", direction: 1 });
  assert.equal(forward.replay?.time, 0.5);

  const playing = reduce(forward, { type: "replay.play", playing: true });
  assert.equal(playing.replay?.playing, true);
  const advanced = reduce(playing, { type: "replay.advance", dt: 0.25 });
  assert.equal(advanced.replay?.time, 0.75);
  assert.equal(advanced.replay?.playing, true);
  const ended = reduce(advanced, { type: "replay.advance", dt: 1 });
  assert.deepEqual(ended.replay && [ended.replay.time, ended.replay.playing], [1, false]);

  // Play at the end runs it again from the start.
  assert.equal(reduce(ended, { type: "replay.play", playing: true }).replay?.time, 0);
  assert.equal(reduce(ended, { type: "replay.exit" }).replay, null);
});

test("a run keeps its station changes on the run clock, and replay puts them back (#284)", () => {
  const run = play([
    { type: "scene", entities: [{ id: "cart", shape: "box", pos: [0, 0, 0] }] },
    { type: "run.started", run: "r1" },
    { type: "signal", node: "sc", port: "a", t: 0, v: 0 },
    { type: "signal", node: "sc", port: "a", t: 1, v: 1 },
    { type: "scene.patch", entities: [{ id: "cart", pos: [1, 0, 0] }] },
    { type: "run.finished", run: "r1", status: "done" },
  ]);
  assert.deepEqual(run.sceneHistory.entitiesAt(0).cart.pos, [0, 0, 0]);
  assert.deepEqual(run.sceneHistory.entitiesAt(1).cart.pos, [1, 0, 0]);

  // A run started after that patches the previous scene as its base, then records its own changes.
  const next = play([{ type: "run.started", run: "r2" }], run);
  assert.equal(next.sceneHistory.moved, false);
  assert.deepEqual(next.sceneHistory.base.cart.pos, [1, 0, 0]);
});

test("an opened record becomes a replay, and its errors are named", () => {
  const r = reduce(fresh(), {
    type: "replay.load",
    record: {
      paperboat: 0,
      run: { id: "saved", status: "done" },
      channels: [{ node: "sc", id: "a", label: "a", t: [0, 2], v: [4, 6] }],
      scene: { frames: [{ t: 2, entities: [{ id: "cart", shape: "box", pos: [2, 0, 0] }] }] },
    },
  });
  assert.equal(r.run?.id, "saved");
  assert.deepEqual(r.replay && [r.replay.from, r.replay.to, r.replay.time], [0, 2, 2]);
  assert.deepEqual(r.sceneHistory.entitiesAt(2).cart.pos, [2, 0, 0]);
  assert.deepEqual(r.traces.valuesAt("sc", 1), { a: 4 });
});
