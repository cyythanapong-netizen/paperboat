import assert from "node:assert/strict";
import { test } from "node:test";
import { backWireExitDistance, backWireLaneOffset, routeWires, type Obstacle, type RouteInput } from "../src/wireRouting.ts";

test("back-wire tracks are distinct and spaced", () => {
  assert.deepEqual([0, 1, 2].map(backWireLaneOffset), [0, 18, 36]);
  assert.deepEqual([0, 1, 2].map(backWireExitDistance), [24, 32, 40]);
});

test("fan-out forward signal wires leave square and split towards each target", () => {
  const wires: RouteInput[] = [
    { id: "w1", source: "A", sourceHandle: "out", target: "B", targetHandle: "in", sx: 100, sy: 100, tx: 300, ty: 50 },
    { id: "w2", source: "A", sourceHandle: "out", target: "C", targetHandle: "in", sx: 100, sy: 100, tx: 300, ty: 150 },
  ];
  const routes = routeWires(wires);
  assert.deepEqual(routes.get("w1")!.points, [[100, 100], [200, 100], [200, 50], [300, 50]]);
  assert.deepEqual(routes.get("w2")!.points, [[100, 100], [200, 100], [200, 150], [300, 150]]);
});

test("nested back-wire loops have distinct horizontal tracks and do not intersect", () => {
  // Inner loop: from x=400 back to x=250 (span 150)
  // Outer loop: from x=500 back to x=100 (span 400)
  const wires: RouteInput[] = [
    { id: "inner", source: "B", target: "A", sx: 400, sy: 100, tx: 250, ty: 100 },
    { id: "outer", source: "C", target: "Root", sx: 500, sy: 100, tx: 100, ty: 100 },
  ];
  const routes = routeWires(wires);
  const rInner = routes.get("inner")!;
  const rOuter = routes.get("outer")!;

  assert.equal(rInner.kind, "back");
  assert.equal(rOuter.kind, "back");
  // Inner loop is closer to blocks (smaller y), outer loop is further down (larger y)
  assert.ok(rInner.labelY < rOuter.labelY, `Inner loop (${rInner.labelY}) should be above outer loop (${rOuter.labelY})`);
  assert.equal(rOuter.labelY - rInner.labelY, 18, "Loops should be separated by backWireLaneOffset");
});

test("back-wires clear intermediate obstacles between endpoints", () => {
  const obstacle: Obstacle = { id: "middle-block", x: 250, y: 80, width: 100, height: 120 }; // bottom = 200
  const wires: RouteInput[] = [
    { id: "loop", source: "C", target: "A", sx: 450, sy: 100, tx: 150, ty: 100 },
  ];
  const routes = routeWires(wires, [obstacle]);
  const r = routes.get("loop")!;
  assert.equal(r.kind, "back");
  // The bottom of the obstacle is 200, so the loop must clear 200 + 28 = 228
  assert.ok(r.labelY >= 228, `Loop track (${r.labelY}) did not clear intermediate obstacle bottom (200 + 28)`);
});

test("forward step wires fanning into the same target get distinct vertical channels", () => {
  const wires: RouteInput[] = [
    { id: "s1", source: "A", target: "StepSink", targetHandle: "step-in", sx: 100, sy: 50, tx: 300, ty: 150, step: true },
    { id: "s2", source: "B", target: "StepSink", targetHandle: "step-in", sx: 100, sy: 250, tx: 300, ty: 150, step: true },
  ];
  const routes = routeWires(wires);
  const r1 = routes.get("s1")!;
  const r2 = routes.get("s2")!;

  assert.equal(r1.kind, "step");
  assert.equal(r2.kind, "step");
  const stem = (r: typeof r1) => r.points[1][0];
  assert.equal(Math.abs(stem(r1) - stem(r2)), 12, "Vertical channels should be spaced by 12px");
});
