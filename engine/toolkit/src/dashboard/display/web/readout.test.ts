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

test("a bigger card lists more of a long vector", () => {
  const long = Array.from({ length: 100 }, (_, i) => i);
  assert.equal(readout(long, undefined, "large").values.split(",").length, 17);
  assert.equal(readout(long, undefined, "xlarge").values.split(",").length, 33);
});

test("a matrix reads its corner as rows, and a one-line preview of its first values", () => {
  assert.deepEqual(readout("2 × 2 matrix: 0, 1; -2, -3"), {
    dims: "2 × 2", type: "matrix", values: "0, 1, -2, -3",
    grid: { rows: [["0", "1"], ["-2", "-3"]], moreColumns: false, moreRows: false },
  });
  const big = readout("1000 × 1000 matrix: 1, 2, …; 3, 4, …; …");
  assert.equal(big.dims, "1000 × 1000");
  assert.equal(big.values, "1, 2, 3, 4, …");
  assert.deepEqual(big.grid, { rows: [["1", "2"], ["3", "4"]], moreColumns: true, moreRows: true });
});

test("a point list keeps its size and shown values", () => {
  assert.deepEqual(readout("5 points: (0, 1), (1, 2), …"), { dims: "5", type: "points", values: "(0, 1), (1, 2), …" });
  assert.deepEqual(readout("1 point: (0, 1)"), { dims: "1", type: "points", values: "(0, 1)" });
});
