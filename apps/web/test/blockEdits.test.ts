import assert from "node:assert/strict";
import { test } from "node:test";
import { flipBlock, rotateBlock } from "../src/blockEdits.ts";
import { makeNode } from "../src/flowModel.ts";

const sum = makeNode({ type: "sum", label: "Sum", kind: "function", inputs: [{ id: "a" }, { id: "b" }], outputs: [{ id: "out" }], params: [] }, "sum-1", 0, 0);

test("a quarter turn moves the ports to the sides they now face", () => {
  assert.deepEqual(rotateBlock(sum, true).data.ports, { top: ["a", "b"], bottom: ["out"] });
  assert.deepEqual(rotateBlock(sum, false).data.ports, { bottom: ["a", "b"], top: ["out"] });
});

test("four quarter turns come back to where they started", () => {
  let n = sum;
  for (let i = 0; i < 4; i++) n = rotateBlock(n, true);
  assert.equal(n.data.rotation ?? 0, 0);
  assert.equal(n.data.mirror, false);
  assert.deepEqual(n.data.ports, { left: ["a", "b"], right: ["out"] });
});

test("a flip mirrors the block; along the sides, port order reverses", () => {
  const x = flipBlock(sum, "x");
  assert.equal(x.data.mirror, true);
  assert.deepEqual(x.data.ports, { right: ["a", "b"], left: ["out"] });
  assert.deepEqual(flipBlock(sum, "y").data.ports, { left: ["b", "a"], right: ["out"] });
  const back = flipBlock(x, "x");
  assert.equal(back.data.mirror, false);
  assert.deepEqual(back.data.ports, { left: ["a", "b"], right: ["out"] });
});

test("the block's other data is kept", () => {
  const noted = { ...sum, data: { ...sum.data, comment: "tune", params: { k: 2 } } };
  assert.equal(rotateBlock(noted, true).data.comment, "tune");
  assert.deepEqual(flipBlock(noted, "x").data.params, { k: 2 });
});
