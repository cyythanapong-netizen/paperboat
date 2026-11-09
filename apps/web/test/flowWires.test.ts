import assert from "node:assert/strict";
import { test } from "node:test";
import { commentedOutIds, iconWireCounts, routeCanvasWires } from "../src/flowWires.ts";
import { makeNode, makeWire, type FlowNode } from "../src/flowModel.ts";

const gain = { type: "gain", label: "Gain", kind: "function", inputs: [{ id: "in" }], outputs: [{ id: "out" }], params: [] } as never;
const wire = (s: string, t: string) => makeWire({ source: s, sourceHandle: "out", target: t, targetHandle: "in" }, "signal");

test("wires are counted per port", () => {
  const nodes = [makeNode(gain, "a", 0, 0), makeNode(gain, "b", 200, 0), makeNode(gain, "c", 200, 200)];
  const { count, missing } = iconWireCounts(nodes, [wire("a", "b"), wire("a", "c")]);
  assert.deepEqual(count.get("a"), { out: 2 });
  assert.deepEqual(count.get("b"), { in: 1 });
  assert.equal(missing.size, 0);
});

test("commented-out blocks, not bypassed ones", () => {
  const nodes: FlowNode[] = [
    { ...makeNode(gain, "a", 0, 0), data: { def: gain, params: {}, commented: true } },
    { ...makeNode(gain, "b", 0, 0), data: { def: gain, params: {}, commented: true, commentMode: "through" } },
    makeNode(gain, "c", 0, 0),
  ];
  assert.deepEqual([...commentedOutIds(nodes)], ["a"]);
});

test("a wire is routed from its source port to its target port", () => {
  // Two measured nodes, as React Flow keeps them: one output on the right of a, one input on the left of b.
  const internal = (id: string, x: number, handles: object) => [
    id,
    { id, position: { x, y: 0 }, measured: { width: 100, height: 60 }, internals: { positionAbsolute: { x, y: 0 }, handleBounds: handles } },
  ];
  const lookup = new Map([
    internal("a", 0, { source: [{ id: "out", x: 96, y: 26, width: 8, height: 8, position: "right" }], target: [] }),
    internal("b", 300, { source: [], target: [{ id: "in", x: -4, y: 26, width: 8, height: 8, position: "left" }] }),
  ] as never);
  const routes = routeCanvasWires(lookup, [wire("a", "b")]);
  const r = routes.get("a.out-b.in")!;
  assert.deepEqual(r.points[0], [100, 30]);
  assert.deepEqual(r.points[r.points.length - 1], [300, 30]);
  assert.equal(routeCanvasWires(lookup, [wire("a", "gone")]).size, 0); // an end that isn't measured: not routed
});
