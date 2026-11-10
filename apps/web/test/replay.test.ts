import assert from "node:assert/strict";
import { test } from "node:test";
import { RecordError, ReplayStore, formatProbe, parseRecord } from "../src/replay.ts";

test("station history puts the scene back at any time (#284)", () => {
  const h = new ReplayStore();
  h.reset({ a: { id: "a", shape: "box", pos: [0, 0, 0] } });
  h.add(1, [{ id: "a", pos: [1, 0, 0] }]);
  h.add(2, [{ id: "a", pos: [2, 0, 0] }, { id: "b", shape: "sphere", pos: [0, 1, 0] }]);
  assert.deepEqual(h.entitiesAt(0).a.pos, [0, 0, 0]);
  assert.deepEqual(h.entitiesAt(1.5).a.pos, [1, 0, 0]);
  assert.deepEqual(h.entitiesAt(2).a.pos, [2, 0, 0]);
  assert.deepEqual(h.entitiesAt(2).b.shape, "sphere"); // a patch may introduce a whole entity
  assert.equal(h.entitiesAt(1).b, undefined);
  assert.equal(h.moved, true);
  h.reset();
  assert.equal(h.moved, false);
});

test("wire probes read a scalar or a vector", () => {
  assert.equal(formatProbe(1.23456), "1.235");
  assert.equal(formatProbe([1, 2.5]), "[1.000, 2.500]");
  assert.equal(formatProbe(0.000123), "1.23e-4");
  assert.equal(formatProbe(undefined), "");
});

test("a record round-trips its channels and rejects what it cannot replay", () => {
  const text = JSON.stringify({
    paperboat: 0,
    engine: "cart-pole",
    run: { id: "run-1", status: "done" },
    channels: [{ node: "sc", id: "a", label: "a", t: [0, 1], v: [2, 3] }],
    scene: { base: {}, frames: [{ t: 1, entities: [{ id: "a", pos: [1, 0, 0] }] }] },
  });
  const r = parseRecord(text);
  assert.equal(r.engine, "cart-pole");
  assert.deepEqual(r.run, { id: "run-1", status: "done" });
  assert.deepEqual(r.channels?.[0].v, [2, 3]);
  assert.deepEqual(r.scene?.frames?.[0].entities[0].id, "a");

  // A foreign writer's signal samples are accepted too.
  const signals = parseRecord(JSON.stringify({ signals: [{ type: "signal", node: "sc", t: 0, v: 1, components: [{ id: "x" }] }] }));
  assert.deepEqual(signals.signals, [{ node: "sc", t: 0, v: 1, components: [{ id: "x" }] }]);

  assert.throws(() => parseRecord("not json"), RecordError);
  assert.throws(() => parseRecord(JSON.stringify({ paperboat: 1, channels: [] })), /unknown record version/);
  assert.throws(() => parseRecord(JSON.stringify({ channels: [] })), /no signals/);
  assert.throws(() => parseRecord(JSON.stringify({ channels: [{ node: "sc", id: "a", t: [0], v: [] }] })), /matching t and v/);
});
