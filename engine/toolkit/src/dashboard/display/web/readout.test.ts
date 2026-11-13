import assert from "node:assert/strict";
import { test } from "node:test";
import { SHOWN, readout } from "./readout.ts";

test("a Display reads out numbers and text as they are", () => {
  assert.deepEqual(readout(1 / 3), { type: "scalar", values: "0.3333" });
  assert.deepEqual(readout("hello"), { values: "hello" });
  assert.deepEqual(readout(undefined), { values: "–" });
});

test("a vector shows its size and values, with labels when it has them", () => {
  assert.deepEqual(readout([1, 2.5, 3]), { dims: "3", type: "vector", values: "1, 2.5, 3" });
  assert.deepEqual(readout([1, 2], "x: 1.0000 m, v: 2.0000 m/s"), { dims: "2", type: "vector", values: "x: 1.0000 m, v: 2.0000 m/s" });
  assert.deepEqual(readout([]), { dims: "0", type: "vector", values: "" });
});

test("a long vector shows its full size and its stats", () => {
  const { dims, values } = readout(Array.from({ length: SHOWN + 5 }, (_, i) => i));
  assert.equal(dims, String(SHOWN + 5));
  assert.equal(values, `min 0, max ${SHOWN + 4}, mean ${(SHOWN + 4) / 2}`);
});

test("a matrix or point list keeps its size and shown values, however big", () => {
  assert.deepEqual(readout("2 × 2 matrix: 0, 1, -2, -3"), { dims: "2 × 2", type: "matrix", values: "0, 1, -2, -3" });
  assert.deepEqual(readout("1000 × 1000 matrix: min -3, max 5, mean 0.2"), { dims: "1000 × 1000", type: "matrix", values: "min -3, max 5, mean 0.2" });
  assert.deepEqual(readout("5 points: (0, 1), (1, 2), …"), { dims: "5", type: "points", values: "(0, 1), (1, 2), …" });
  assert.deepEqual(readout("1 point: (0, 1)"), { dims: "1", type: "points", values: "(0, 1)" });
});
