import assert from "node:assert/strict";
import { test } from "node:test";
import { connect, freeId, hasStepPorts, makeNode, makeWire, missingDef, resolveBlocks, syncLinked, withoutLooseWires, type FlowNode, type Wire } from "../src/flowModel.ts";
import type { BlockDef } from "../src/protocol.ts";

const gain: BlockDef = { type: "gain", label: "Gain", kind: "function", inputs: [{ id: "in" }], outputs: [{ id: "out" }], params: [{ id: "k", type: "number", default: 1 }] };
const step: BlockDef = { type: "wait", label: "Wait", kind: "function", inputs: [{ id: "go", type: "step" }], outputs: [{ id: "next", type: "step" }], params: [] };
const cart: BlockDef = { ...gain, type: "cart", label: "Cart", linked: true, params: [] };

test("a new block takes its params' defaults; a linked one can't be deleted", () => {
  const n = makeNode(gain, "gain-1", 10, 20, { k: 3 });
  assert.deepEqual(n.data.params, { k: 3 });
  assert.equal(makeNode(gain, "gain-2", 0, 0).data.params.k, 1);
  assert.equal(n.deletable, true);
  assert.equal(makeNode(cart, "c", 0, 0).deletable, false);
});

test("free ids count up from 1", () => {
  const nodes = [makeNode(gain, "gain-1", 0, 0), makeNode(gain, "gain-3", 0, 0)];
  assert.equal(freeId("gain", nodes), "gain-2");
  assert.equal(freeId("sum", nodes), "sum-1");
});

test("step wires carry their type; ids name both ends", () => {
  const w = makeWire({ source: "a", sourceHandle: "next", target: "b", targetHandle: "go" }, "step");
  assert.equal(w.id, "a.next-b.go");
  assert.equal(w.data?.type, "step"); // the arrow is drawn by WireEdge, outside the target port
  assert.equal(makeWire({ source: "a", sourceHandle: "out", target: "b", targetHandle: "in" }, "signal").data?.type, "signal");
});

test("a signal input reads one wire; a step output leads to one block", () => {
  const ws: Wire[] = [makeWire({ source: "a", sourceHandle: "out", target: "c", targetHandle: "in" }, "signal")];
  const signal = connect(ws, { source: "b", sourceHandle: "out", target: "c", targetHandle: "in" }, "signal");
  assert.deepEqual(signal.map((w) => w.source), ["b"]);
  const steps: Wire[] = [makeWire({ source: "a", sourceHandle: "next", target: "b", targetHandle: "go" }, "step")];
  const moved = connect(steps, { source: "a", sourceHandle: "next", target: "c", targetHandle: "go" }, "step");
  assert.deepEqual(moved.map((w) => w.target), ["c"]);
  const joined = connect(steps, { source: "x", sourceHandle: "next", target: "b", targetHandle: "go" }, "step");
  assert.equal(joined.length, 2); // a step input can be reached from many
});

test("a missing block keeps the ports its wires use and its params", () => {
  const def = missingDef("old", { k: 2, on: true, name: "x" }, [{ source: "m", sourceHandle: "y", target: "z", targetHandle: "in", type: "step" }, { source: "a", sourceHandle: "out", target: "m", targetHandle: "u" }], "m");
  assert.deepEqual(def.inputs, [{ id: "u", type: "signal" }]);
  assert.deepEqual(def.outputs, [{ id: "y", type: "step" }]);
  assert.deepEqual(def.params.map((p) => p.type), ["number", "boolean", "text"]);
  assert.equal(hasStepPorts(def), true);
  assert.equal(hasStepPorts(gain), false);
  assert.equal(hasStepPorts(step), true);
});

test("linked blocks follow the station's objects", () => {
  const objects = [{ type: "cart", label: "Cart", block: "cart" }] as never;
  const nodes: FlowNode[] = [makeNode(gain, "gain-1", 0, 0)];
  const added = syncLinked(nodes, { objects: [{ id: "cart-1", type: "cart" }] } as never, objects, [gain, cart]);
  assert.deepEqual(added.map((n) => n.id), ["gain-1", "cart-1"]);
  assert.equal(syncLinked(added, { objects: [{ id: "cart-1", type: "cart" }] } as never, objects, [gain, cart]), added); // nothing to do: the same array
  assert.deepEqual(syncLinked(added, { objects: [] }, objects, [gain, cart]).map((n) => n.id), ["gain-1"]);
});

test("a block whose type goes is missing, and whole again when it comes back", () => {
  const nodes: FlowNode[] = [makeNode(gain, "gain-1", 0, 0, { k: 5 })];
  assert.equal(resolveBlocks(nodes, [], [gain]), nodes);
  const gone = resolveBlocks(nodes, [], []);
  assert.equal(gone[0].type === "block" && gone[0].data.missing, true);
  const gain2: BlockDef = { ...gain, params: [{ id: "k", type: "number", default: 1 }, { id: "bias", type: "number", default: 0 }] };
  const back = resolveBlocks(gone, [], [gain2]);
  assert.ok(back[0].type === "block");
  assert.deepEqual(back[0].type === "block" && back[0].data.params, { k: 5, bias: 0 });
  assert.equal(back[0].type === "block" && back[0].data.missing, undefined);
});

test("wires that lost a block or a port go; groups and missing blocks keep theirs", () => {
  const a = makeNode(gain, "a", 0, 0);
  const b = makeNode(gain, "b", 0, 0);
  const lost = { ...makeNode(gain, "lost", 0, 0), data: { def: gain, params: {}, missing: true } };
  const edges = [
    makeWire({ source: "a", sourceHandle: "out", target: "b", targetHandle: "in" }, "signal"),
    makeWire({ source: "a", sourceHandle: "gone", target: "b", targetHandle: "in" }, "signal"),
    makeWire({ source: "a", sourceHandle: "out", target: "c", targetHandle: "in" }, "signal"),
    makeWire({ source: "lost", sourceHandle: "whatever", target: "b", targetHandle: "in" }, "signal"),
  ];
  assert.deepEqual(withoutLooseWires([a, b, lost], edges).map((w) => w.id), ["a.out-b.in", "lost.whatever-b.in"]);
  const kept = edges.slice(0, 1);
  assert.equal(withoutLooseWires([a, b], kept), kept); // all stay: the same array
});
