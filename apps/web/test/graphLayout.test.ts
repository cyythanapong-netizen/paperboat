import assert from "node:assert/strict";
import { test } from "node:test";
import { countCrossings, layoutGraph, type LayoutEdge, type LayoutNode } from "../src/graphLayout.ts";

const entries = (nodes: readonly LayoutNode[], edges: readonly LayoutEdge[], options?: Parameters<typeof layoutGraph>[2]) =>
  [...layoutGraph(nodes, edges, options).entries()];
const point = (nodes: readonly LayoutNode[], edges: readonly LayoutEdge[], id: string) => layoutGraph(nodes, edges).get(id)!;

test("an empty graph has an empty layout", () => {
  assert.deepEqual(entries([], []), []);
});

test("chains use longest-path layers, default node widths, and the default column gap", () => {
  const nodes = [{ id: "c" }, { id: "a" }, { id: "b" }];
  const edges = [
    { source: "a", target: "c" }, // a bypass must not pull c into b's layer
    { source: "b", target: "c" },
    { source: "a", target: "b" },
  ];
  assert.deepEqual(entries(nodes, edges), [
    ["a", { x: 0, y: 0 }],
    ["b", { x: 300, y: 0 }],
    ["c", { x: 600, y: 0 }],
  ]);
});

test("fan-out children have deterministic ID order and rows use the default height and gap", () => {
  const nodes = [{ id: "c" }, { id: "root" }, { id: "a" }, { id: "b" }];
  const edges = [
    { source: "root", target: "c" },
    { source: "root", target: "a" },
    { source: "root", target: "b" },
  ];
  assert.deepEqual(point(nodes, edges, "root"), { x: 0, y: 0 });
  assert.deepEqual(point(nodes, edges, "a"), { x: 300, y: 0 });
  assert.deepEqual(point(nodes, edges, "b"), { x: 300, y: 170 });
  assert.deepEqual(point(nodes, edges, "c"), { x: 300, y: 340 });
});

test("fan-in sources are stable and their sink is in the next column", () => {
  const nodes = [{ id: "sink" }, { id: "b" }, { id: "a" }];
  const edges = [
    { source: "b", target: "sink" },
    { source: "a", target: "sink" },
  ];
  assert.deepEqual(entries(nodes, edges), [
    ["a", { x: 0, y: 0 }],
    ["b", { x: 0, y: 170 }],
    ["sink", { x: 300, y: 85 }], // centered vertically between fan-in sources a (0) and b (170) to eliminate bend
  ]);
});

test("predecessor barycenters order a layer to avoid a crossing", () => {
  const nodes = [{ id: "y" }, { id: "b" }, { id: "z" }, { id: "a" }];
  const edges = [
    { source: "a", target: "z" },
    { source: "b", target: "y" },
  ];
  const laidOut = layoutGraph(nodes, edges);
  assert.deepEqual(laidOut.get("a"), { x: 0, y: 0 });
  assert.deepEqual(laidOut.get("b"), { x: 0, y: 170 });
  assert.deepEqual(laidOut.get("z"), { x: 300, y: 0 });
  assert.deepEqual(laidOut.get("y"), { x: 300, y: 170 });
});

test("cycles are broken deterministically into a finite acyclic layering", () => {
  const nodes = [{ id: "d" }, { id: "c" }, { id: "b" }, { id: "a" }];
  const edges = [
    { source: "a", target: "b" },
    { source: "b", target: "a" },
    { source: "b", target: "c" },
    { source: "c", target: "a" },
  ];
  assert.deepEqual(entries(nodes, edges), [
    ["a", { x: 0, y: 0 }],
    ["b", { x: 300, y: 0 }],
    ["c", { x: 600, y: 0 }],
    ["d", { x: 0, y: 170 }],
  ]);
});

test("disconnected nodes share the first column in ID order", () => {
  assert.deepEqual(entries([{ id: "z" }, { id: "a" }, { id: "m" }], []), [
    ["a", { x: 0, y: 0 }],
    ["m", { x: 0, y: 170 }],
    ["z", { x: 0, y: 340 }],
  ]);
});

test("unknown endpoints, duplicate edges, and self-links do not change the layout", () => {
  const nodes = [{ id: "c" }, { id: "a" }, { id: "b" }];
  const clean = [
    { source: "a", target: "b" },
    { source: "b", target: "c" },
  ];
  const noisy = [
    { source: "a", target: "a" },
    { source: "missing", target: "a" },
    { source: "b", target: "c" },
    { source: "a", target: "b" },
    { source: "a", target: "b" },
    { source: "c", target: "missing" },
    { source: "b", target: "b" },
  ];
  assert.deepEqual(entries(nodes, noisy), entries(nodes, clean));
  assert.equal(layoutGraph(nodes, noisy).has("missing"), false);
});

test("column widths, row heights, custom gaps, and origin use the measured dimensions", () => {
  const nodes = [
    { id: "d", width: 80, height: 90 },
    { id: "b", width: 300, height: 60 },
    { id: "c", width: 50, height: 70 },
    { id: "a", width: 100, height: 40 },
  ];
  const edges = [
    { source: "b", target: "d" },
    { source: "a", target: "c" },
  ];
  assert.deepEqual(entries(nodes, edges, { columnGap: 25, rowGap: 10, origin: { x: 10, y: 20 } }), [
    ["a", { x: 10, y: 20 }],
    ["b", { x: 10, y: 70 }],
    ["c", { x: 335, y: 20 }],
    ["d", { x: 335, y: 100 }],
  ]);
});

test("non-positive or non-finite dimensions independently fall back to 220 by 130", () => {
  const nodes = [
    { id: "isolated", width: Number.POSITIVE_INFINITY, height: -4 },
    { id: "b", width: Number.NaN, height: 0 },
    { id: "a", width: 0, height: Number.NaN },
    { id: "c" },
  ];
  const edges = [
    { source: "a", target: "b" },
    { source: "b", target: "c" },
  ];
  assert.deepEqual(entries(nodes, edges), [
    ["a", { x: 0, y: 0 }],
    ["b", { x: 300, y: 0 }],
    ["c", { x: 600, y: 0 }],
    ["isolated", { x: 0, y: 170 }],
  ]);
});

test("zero gaps are respected and fractional origins produce integer coordinates", () => {
  const nodes = [
    { id: "a", width: 100, height: 50 },
    { id: "b", width: 100, height: 50 },
    { id: "c", width: 100, height: 50 },
  ];
  const edges = [{ source: "a", target: "c" }];
  assert.deepEqual(entries(nodes, edges, { columnGap: 0, rowGap: 0, origin: { x: 10.4, y: 20.6 } }), [
    ["a", { x: 10, y: 21 }],
    ["b", { x: 10, y: 71 }],
    ["c", { x: 110, y: 21 }],
  ]);
  for (const p of layoutGraph(nodes, edges).values()) {
    assert.ok(Number.isInteger(p.x));
    assert.ok(Number.isInteger(p.y));
  }
});

test("fractional dimensions and zero gaps still produce non-overlapping rectangles", () => {
  const nodes = [
    { id: "a", width: 90.25, height: 50.2 },
    { id: "b", width: 140.5, height: 75.25 },
    { id: "c", width: 60.25, height: 100.5 },
    { id: "d", width: 200.75, height: 30.5 },
    { id: "e", width: 80.5, height: 65.75 },
    { id: "f", width: 100.2, height: 40.2 },
  ];
  const edges = [
    { source: "a", target: "c" },
    { source: "a", target: "d" },
    { source: "b", target: "c" },
    { source: "b", target: "e" },
    { source: "c", target: "f" },
    { source: "d", target: "f" },
    { source: "e", target: "f" },
  ];
  const laidOut = layoutGraph(nodes, edges, { columnGap: 0, rowGap: 0 });
  const rectangles = nodes.map((node) => ({ id: node.id, ...laidOut.get(node.id)!, width: node.width, height: node.height }));
  for (let a = 0; a < rectangles.length; a++) {
    for (let b = a + 1; b < rectangles.length; b++) {
      const one = rectangles[a];
      const two = rectangles[b];
      const overlaps = one.x < two.x + two.width && two.x < one.x + one.width && one.y < two.y + two.height && two.y < one.y + one.height;
      assert.equal(overlaps, false, `${one.id} overlaps ${two.id}`);
    }
  }
});

test("layout does not mutate nodes, edges, or options", () => {
  const nodes = Object.freeze([
    Object.freeze({ id: "b", width: 120, height: 70 }),
    Object.freeze({ id: "a", width: 100, height: 60 }),
  ]);
  const edges = Object.freeze([Object.freeze({ source: "a", target: "b" })]);
  const options = Object.freeze({ origin: Object.freeze({ x: 15, y: 25 }), columnGap: 30, rowGap: 20 });
  const before = structuredClone({ nodes, edges, options });
  layoutGraph(nodes, edges, options);
  assert.deepEqual({ nodes, edges, options }, before);
});

test("shuffling nodes and edges never changes positions or Map iteration order", () => {
  const nodes: LayoutNode[] = [
    { id: "a", width: 100, height: 60 },
    { id: "b", width: 130, height: 80 },
    { id: "c", width: 90, height: 70 },
    { id: "d", width: 160, height: 100 },
    { id: "e", width: 110, height: 50 },
    { id: "isolated" },
  ];
  const edges: LayoutEdge[] = [
    { source: "a", target: "c" },
    { source: "b", target: "c" },
    { source: "c", target: "d" },
    { source: "d", target: "a" }, // a deterministic back edge after traversal
    { source: "b", target: "e" },
    { source: "e", target: "d" },
    { source: "a", target: "c" },
  ];
  const options = { columnGap: 35, rowGap: 15, origin: { x: 20, y: 30 } };
  const expected = entries(nodes, edges, options);
  let seed = 0x103;
  const random = () => {
    seed = (seed * 1_664_525 + 1_013_904_223) >>> 0;
    return seed / 0x1_0000_0000;
  };
  const shuffle = <T>(input: readonly T[]) => {
    const result = [...input];
    for (let i = result.length - 1; i > 0; i--) {
      const j = Math.floor(random() * (i + 1));
      [result[i], result[j]] = [result[j], result[i]];
    }
    return result;
  };
  for (let run = 0; run < 30; run++) assert.deepEqual(entries(shuffle(nodes), shuffle(edges), options), expected);
});

test("port-order awareness avoids crossings between diverging ports", () => {
  // A has two outputs: o1 (top, offset 30) and o2 (bottom, offset 100)
  // o1 connects to B; o2 connects to C
  // Crossing minimization should place B above C so the wires do not cross
  const nodes = [{ id: "A" }, { id: "B" }, { id: "C" }];
  const edges: LayoutEdge[] = [
    { source: "A", target: "B", sourceOffset: 30, targetOffset: 65 },
    { source: "A", target: "C", sourceOffset: 100, targetOffset: 65 },
  ];
  const laidOut = layoutGraph(nodes, edges);
  const posB = laidOut.get("B")!;
  const posC = laidOut.get("C")!;
  assert.ok(posB.y < posC.y, "B (fed by top port) must be placed above C (fed by bottom port)");
});

test("a 4-block chain aligns horizontally without unnecessary bends", () => {
  const nodes = [{ id: "a" }, { id: "b" }, { id: "c" }, { id: "d" }];
  const edges = [
    { source: "a", target: "b" },
    { source: "b", target: "c" },
    { source: "c", target: "d" },
  ];
  const laidOut = layoutGraph(nodes, edges);
  assert.equal(laidOut.get("a")!.y, laidOut.get("b")!.y);
  assert.equal(laidOut.get("b")!.y, laidOut.get("c")!.y);
  assert.equal(laidOut.get("c")!.y, laidOut.get("d")!.y);
});

test("countCrossings accurately detects inverted edge pairs across layers", () => {
  const columns = [["a", "b"], ["c", "d"]];
  const uncrossed: LayoutEdge[] = [
    { source: "a", target: "c" },
    { source: "b", target: "d" },
  ];
  const crossed: LayoutEdge[] = [
    { source: "a", target: "d" },
    { source: "b", target: "c" },
  ];
  const yMap = new Map([["a", 0], ["b", 100], ["c", 0], ["d", 100]]);
  const sizes = new Map([
    ["a", { width: 100, height: 50 }],
    ["b", { width: 100, height: 50 }],
    ["c", { width: 100, height: 50 }],
    ["d", { width: 100, height: 50 }],
  ]);
  assert.equal(countCrossings(columns, uncrossed, yMap, sizes), 0);
  assert.equal(countCrossings(columns, crossed, yMap, sizes), 1);
});

