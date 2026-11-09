import assert from "node:assert/strict";
import { test } from "node:test";
import { initial, MAX_METRIC_POINTS, reduce, runWord, type EngineState } from "../src/engine.ts";
import type { ServerMsg } from "../src/protocol.ts";

const play = (messages: ServerMsg[], from: EngineState = initial) => messages.reduce(reduce, from);
const sets = (s: EngineState) => ({ active: new Set(s.active), failed: new Set(s.failed), queued: new Set(s.queued), cached: new Set(s.cached) });
const state = (node: string, value: "queued" | "running" | "done" | "cached" | "failed"): ServerMsg => ({ type: "node.state", node, state: value });

test("node.state: queued, then running, then done or cached; one at a time per block", () => {
  const s1 = play([{ type: "run.started", run: "r1" }, state("a", "queued"), state("b", "queued"), state("c", "queued")]);
  assert.deepEqual(s1.queued, ["a", "b", "c"]);
  const s2 = play([state("a", "cached"), state("b", "running")], s1);
  assert.deepEqual(s2.queued, ["c"]);
  assert.deepEqual(s2.active, ["b"]);
  assert.deepEqual(s2.cached, ["a"]);
  assert.equal(runWord(sets(s2), "a"), "cached");
  assert.equal(runWord(sets(s2), "b"), "running");
  assert.equal(runWord(sets(s2), "c"), "queued");
  const s3 = play([state("b", "done")], s2);
  assert.equal(runWord(sets(s3), "b"), undefined); // done says nothing
  // A block that fails: failed wins over what it was; what never ran is no longer waiting once the run ends.
  const s4 = play([state("c", "running"), state("c", "failed"), { type: "run.finished", run: "r1", status: "failed" }], s3);
  assert.equal(runWord(sets(s4), "c"), "failed");
  assert.deepEqual(s4.queued, []);
  assert.deepEqual(s4.cached, ["a"]); // what the run reused stays said until the next one
  const s5 = play([{ type: "run.finished", run: "r1", status: "cancelled" }], play([state("x", "queued")], s4));
  assert.deepEqual(s5.queued, []);
  const s6 = play([{ type: "run.started", run: "r2" }], s5);
  assert.deepEqual([s6.queued, s6.cached, s6.failed, s6.active], [[], [], [], []]);
});

test("metric: each block's series in order, with its unit, capped", () => {
  const s = play([
    { type: "run.started", run: "r" },
    { type: "metric", node: "solve", series: "residual", x: 0, y: 1 },
    { type: "metric", node: "solve", series: "residual", x: 1, y: 0.5 },
    { type: "metric", node: "solve", series: "cost", x: 10, y: 4, unit: "J" },
  ]);
  assert.deepEqual(s.metrics.solve.residual, { points: [[0, 1], [1, 0.5]] });
  assert.deepEqual(s.metrics.solve.cost, { points: [[10, 4]], unit: "J" });
  let many = s;
  for (let i = 0; i < MAX_METRIC_POINTS + 5; i++) many = reduce(many, { type: "metric", node: "n", series: "k", x: i, y: i });
  assert.equal(many.metrics.n.k.points.length, MAX_METRIC_POINTS);
  assert.deepEqual(many.metrics.n.k.points.at(-1), [MAX_METRIC_POINTS + 4, MAX_METRIC_POINTS + 4]);
  assert.deepEqual(reduce(many, { type: "run.started", run: "next" }).metrics, {});
});

test("monitor.state: the latest per monitor and input; frame: the latest per block; neither is an unknown message", () => {
  const s = play([
    { type: "run.started", run: "r" },
    { type: "monitor.state", node: "lamp", state: "warn", t: 0.6, v: 0.6 },
    { type: "monitor.state", node: "lamp", state: "trip", t: 1.1, v: 1.1 },
    { type: "monitor.state", node: "scope", state: "trip", t: 2, port: "b" },
    { type: "frame", node: "view", t: 0, shapes: [{ t: "circle", p: [[0, 0]], r: 1 }] },
    { type: "frame", node: "view", t: 0.5, field: { w: 1, h: 2, v: [0, null] } },
  ]);
  assert.deepEqual(s.monitorStates.lamp, { in: { state: "trip", t: 1.1, v: 1.1 } });
  assert.deepEqual(s.monitorStates.scope, { b: { state: "trip", t: 2 } });
  assert.equal(s.frames.view.t, 0.5);
  assert.deepEqual(s.frames.view.field, { w: 1, h: 2, v: [0, null] });
  assert.equal(s.logs.length, 0);
  const next = reduce(s, { type: "run.started", run: "r2" });
  assert.deepEqual([next.monitorStates, next.frames], [{}, {}]);
});
