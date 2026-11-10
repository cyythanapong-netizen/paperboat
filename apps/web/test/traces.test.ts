import assert from "node:assert/strict";
import { test } from "node:test";
import { MAX_POINTS, TraceBuffer, TraceStore } from "../src/traces.ts";

test("a trace buffer appends in O(1) and thins to keep the whole span", () => {
  const b = new TraceBuffer(8);
  for (let i = 0; i < 9; i++) b.push(i, i * 10);
  assert.ok(b.n <= 8);
  assert.equal(b.times[0], 0);
  assert.equal(b.times[b.n - 1], 8); // the latest is always kept
  assert.equal(b.last, 80);
});

test("10^6 samples append within a fixed budget", () => {
  const b = new TraceBuffer();
  const start = performance.now();
  for (let i = 0; i < 1e6; i++) b.push(i * 1e-3, Math.sin(i));
  const ms = performance.now() - start;
  assert.ok(b.n <= MAX_POINTS && b.times[b.n - 1] === 999.999);
  assert.ok(ms < 1000, `took ${ms.toFixed(0)} ms`);
});

test("the store makes one channel per input, and per component of a vector", () => {
  const s = new TraceStore();
  s.add({ node: "sc", port: "a", t: 0, v: 1 });
  s.add({ node: "sc", port: "c", t: 0, v: [1, 2], components: [{ id: "x", unit: "m" }, { id: "v" }] });
  s.add({ node: "g", t: 0, v: "text" });
  assert.deepEqual(s.channels("sc").map((c) => [c.id, c.label, c.unit]), [["a", "a", undefined], ["c.x", "c.x", "m"], ["c.v", "c.v", undefined]]);
  assert.equal(s.channels("g").length, 0);
  s.add({ node: "g", t: 0, v: [5, 6] }); // a Graph's vector: components by index
  assert.deepEqual(s.channels("g").map((c) => c.label), ["0", "1"]);
});

test("replay reads the span, the sample spacing and each input's value at a time (#284)", () => {
  const s = new TraceStore();
  for (let i = 0; i < 4; i++) s.add({ node: "sc", port: "a", t: i * 0.5, v: i });
  s.add({ node: "sc", port: "c", t: 0.5, v: [10, 20], components: [{ id: "x" }, { id: "y" }] });
  assert.deepEqual(s.span(), [0, 1.5]);
  assert.equal(s.step(), 0.5);
  // The latest sample at or before t, like a zero-order hold.
  assert.deepEqual(s.valuesAt("sc", 0.6), { a: 1, c: [10, 20] });
  assert.deepEqual(s.valuesAt("sc", 1.4), { a: 2, c: [10, 20] });
  assert.equal(s.valuesAt("sc", -1), undefined); // before the first sample
  assert.equal(s.valuesAt("gone", 0), undefined);
});

test("a record loads channels back into the store", () => {
  const s = new TraceStore();
  s.load([
    { node: "sc", id: "a", label: "a", t: [0, 1], v: [2, 3] },
    { node: "sc", id: "c.x", label: "x", unit: "m", t: [0, 1], v: [4, 5] },
  ]);
  assert.deepEqual(s.span(), [0, 1]);
  assert.deepEqual(s.valuesAt("sc", 1), { a: 3, c: [5] });
  assert.deepEqual(s.channels("sc").map((c) => [c.id, c.unit]), [["a", undefined], ["c.x", "m"]]);
});
