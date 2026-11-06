import assert from "node:assert/strict";
import { test } from "node:test";
import {
  backWireExitDistance,
  backWireLaneOffset,
  orderWireRoutes,
  routeWires,
  sampleCubicBezier,
  wireCurvature,
  type Obstacle,
  type RouteInput,
} from "../src/wireRouting.ts";

test("fan-out lanes follow destination order with stable ties", () => {
  const routes = orderWireRoutes([
    { id: "z", y: 100, handle: "in" },
    { id: "b", y: 20, handle: "in" },
    { id: "a", y: 100, handle: "in" },
    { id: "c", y: 100, handle: "a" },
  ]);
  assert.deepEqual([...routes], [["b", 0], ["c", 1], ["a", 2], ["z", 3]]);
});

test("fan-in and fan-out curves use their crowded end's order", () => {
  assert.equal(wireCurvature(0, 1, 0, 1), 0.25);
  assert.deepEqual([0, 1, 2].map((lane) => wireCurvature(0, 1, lane, 3)), [0.15, 0.25, 0.35]);
  assert.deepEqual([0, 1, 2].map((lane) => wireCurvature(lane, 3, 2 - lane, 3)), [0.25, 0.25, 0.25]);
  assert.ok(wireCurvature(0, 8, 0, 1) >= 0.12);
});

test("back-wire tracks are distinct and spaced", () => {
  assert.deepEqual([0, 1, 2].map(backWireLaneOffset), [0, 18, 36]);
  assert.deepEqual([0, 1, 2].map(backWireExitDistance), [24, 32, 40]);
});

test("fan-out forward signal wires diverge immediately and do not overlap", () => {
  const wires: RouteInput[] = [
    { id: "w1", source: "A", sourceHandle: "out", target: "B", targetHandle: "in", sx: 100, sy: 100, tx: 300, ty: 50 },
    { id: "w2", source: "A", sourceHandle: "out", target: "C", targetHandle: "in", sx: 100, sy: 100, tx: 300, ty: 150 },
    { id: "w3", source: "A", sourceHandle: "out", target: "D", targetHandle: "in", sx: 100, sy: 100, tx: 300, ty: 250 },
  ];
  const routes = routeWires(wires);
  assert.equal(routes.size, 3);

  // Parse control points from path "M sx,sy C cx1,cy1 cx2,cy2 tx,ty"
  const parseBezier = (path: string) => {
    const match = /^M\s*([\d.-]+),([\d.-]+)\s*C\s*([\d.-]+),([\d.-]+)\s*([\d.-]+),([\d.-]+)\s*([\d.-]+),([\d.-]+)$/.exec(path);
    assert.ok(match, `Invalid bezier path: ${path}`);
    const [, sx, sy, cx1, cy1, cx2, cy2, tx, ty] = match.map(Number);
    return {
      p0: [sx, sy] as [number, number],
      p1: [cx1, cy1] as [number, number],
      p2: [cx2, cy2] as [number, number],
      p3: [tx, ty] as [number, number],
    };
  };

  const samples1 = sampleCubicBezier(parseBezier(routes.get("w1")!.path).p0, parseBezier(routes.get("w1")!.path).p1, parseBezier(routes.get("w1")!.path).p2, parseBezier(routes.get("w1")!.path).p3, 20);
  const samples2 = sampleCubicBezier(parseBezier(routes.get("w2")!.path).p0, parseBezier(routes.get("w2")!.path).p1, parseBezier(routes.get("w2")!.path).p2, parseBezier(routes.get("w2")!.path).p3, 20);
  const samples3 = sampleCubicBezier(parseBezier(routes.get("w3")!.path).p0, parseBezier(routes.get("w3")!.path).p1, parseBezier(routes.get("w3")!.path).p2, parseBezier(routes.get("w3")!.path).p3, 20);

  // Beyond ~16px from port (sample index >= 2 in 200px span, approx x >= 118)
  for (let i = 2; i <= 18; i++) {
    const dist12 = Math.hypot(samples1[i][0] - samples2[i][0], samples1[i][1] - samples2[i][1]);
    const dist23 = Math.hypot(samples2[i][0] - samples3[i][0], samples2[i][1] - samples3[i][1]);
    assert.ok(dist12 >= 4, `w1 and w2 too close at sample ${i}: ${dist12}px`);
    assert.ok(dist23 >= 4, `w2 and w3 too close at sample ${i}: ${dist23}px`);
  }
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
  assert.notEqual(r1.labelX, r2.labelX, "Vertical channels must have distinct x coordinates");
  assert.equal(Math.abs(r1.labelX - r2.labelX), 12, "Vertical channels should be spaced by 12px");
});
