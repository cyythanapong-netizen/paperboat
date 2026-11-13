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

test("a long vector shows its first values and its full size", () => {
  const { dims, values } = readout(Array.from({ length: 100 }, (_, i) => i / 10));
  assert.equal(dims, "100");
  assert.equal(values, "0, 0.1, 0.2, 0.3, 0.4, 0.5, …");
  assert.equal(values.split(",").length, SHOWN + 1);
});

test("a matrix or point list keeps its size and shown values, however big", () => {
  assert.deepEqual(readout("2 × 2 matrix: 0, 1, -2, -3"), { dims: "2 × 2", type: "matrix", values: "0, 1, -2, -3" });
  assert.deepEqual(readout("1000 × 1000 matrix: 1, 2, 3, 4, 5, 6, …"), { dims: "1000 × 1000", type: "matrix", values: "1, 2, 3, 4, 5, 6, …" });
  assert.deepEqual(readout("5 points: (0, 1), (1, 2), …"), { dims: "5", type: "points", values: "(0, 1), (1, 2), …" });
  assert.deepEqual(readout("1 point: (0, 1)"), { dims: "1", type: "points", values: "(0, 1)" });
});
