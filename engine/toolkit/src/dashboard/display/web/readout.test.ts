import assert from "node:assert/strict";
import { test } from "node:test";
import { SHOWN, readout } from "./readout.ts";

test("a Display reads out numbers and text as they are", () => {
  assert.deepEqual(readout(1 / 3), { text: "0.3333" });
  assert.deepEqual(readout("5 points: (0, 1), …"), { text: "5 points: (0, 1), …" });
  assert.deepEqual(readout(undefined), { text: "–" });
});

test("a short list or vector is shown whole, with its labels when it has them, and counted", () => {
  assert.deepEqual(readout([1, 2.5, 3]), { text: "[1, 2.5, 3]", count: 3 });
  assert.deepEqual(readout([1, 2], "x: 1.0000 m, v: 2.0000 m/s"), { text: "[x: 1.0000 m, v: 2.0000 m/s]", count: 2 });
  assert.deepEqual(readout([]), { text: "[]", count: 0 });
});

test("a long list shows its first values and how many there are", () => {
  const list = Array.from({ length: 100 }, (_, i) => i / 10);
  const { text, count } = readout(list, "0: 0.0000, 1: 0.1000, …every one of them");
  assert.equal(count, 100);
  assert.equal(text, "[0, 0.1, 0.2, 0.3, 0.4, 0.5, …]");
  assert.equal(text.split(",").length, SHOWN + 1);
  const labelled = readout(Array.from({ length: 8 }, (_, i) => i), "x: 0, y: 1, z: 2, vx: 3, vy: 4, vz: 5, w: 6, q: 7");
  assert.equal(labelled.text, "[x: 0, y: 1, z: 2, vx: 3, vy: 4, vz: 5, …]");
});
