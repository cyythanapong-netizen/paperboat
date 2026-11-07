export type Side = "left" | "right" | "top" | "bottom";

export type RouteInput = {
  id: string;
  source: string;
  sourceHandle?: string | null;
  target: string;
  targetHandle?: string | null;
  sx: number;
  sy: number;
  tx: number;
  ty: number;
  sourcePosition?: Side;
  targetPosition?: Side;
  step?: boolean;
};

export type Obstacle = {
  id: string;
  x: number;
  y: number;
  width: number;
  height: number;
};

export type WireRoute = {
  id: string;
  kind: "forward" | "back" | "step";
  path: string;
  labelX: number;
  labelY: number;
};

export type WireRouteTarget = { id: string; y: number; handle?: string | null };

const byText = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);

/** Wires grouped by the port they leave (`source`) or reach (`target`), each group in input order. */
function byPort(wires: readonly RouteInput[], end: "source" | "target"): Map<string, RouteInput[]> {
  const groups = new Map<string, RouteInput[]>();
  for (const w of wires) {
    const key = end === "source" ? `${w.source}:${w.sourceHandle ?? ""}` : `${w.target}:${w.targetHandle ?? ""}`;
    const list = groups.get(key) ?? [];
    list.push(w);
    groups.set(key, list);
  }
  return groups;
}

/** Assign fan-out lanes in the same vertical order as their destinations. */
export function orderWireRoutes(routes: readonly WireRouteTarget[]): Map<string, number> {
  const ordered = [...routes].sort(
    (a, b) => a.y - b.y || byText(a.handle ?? "", b.handle ?? "") || byText(a.id, b.id),
  );
  return new Map(ordered.map((route, lane) => [route.id, lane]));
}

/** Keep fan-out curves close, but give each one a distinct bend around the default curve. */
export function fanOutCurvature(lane: number, count: number): number {
  if (count <= 1) return 0.25;
  return Math.max(0.12, 0.25 + (lane - (count - 1) / 2) * 0.1);
}

/** Use source and target ordering when both ends are crowded; otherwise use the crowded end. */
export function wireCurvature(sourceLane: number, sourceCount: number, targetLane: number, targetCount: number): number {
  const bends = [
    ...(sourceCount > 1 ? [fanOutCurvature(sourceLane, sourceCount)] : []),
    ...(targetCount > 1 ? [fanOutCurvature(targetLane, targetCount)] : []),
  ];
  return bends.length ? bends.reduce((sum, bend) => sum + bend, 0) / bends.length : 0.25;
}

/** Spacing between the horizontal tracks used by back-wires. */
export function backWireLaneOffset(lane: number): number {
  return Math.max(0, lane) * 18;
}

/** Move each loop's vertical stem sideways too, so its route diverges soon after the port. */
export function backWireExitDistance(lane: number): number {
  return 24 + Math.max(0, lane) * 8;
}

/**
 * Produce an SVG path string for a sequence of points with rounded corners.
 * Corners use quadratic beziers (Q) connecting tangent lines at radius `r`.
 */
export function roundedPolyline(points: readonly [number, number][], radius = 10): string {
  if (points.length === 0) return "";
  if (points.length === 1) return `M ${points[0][0]},${points[0][1]}`;
  if (points.length === 2) return `M ${points[0][0]},${points[0][1]} L ${points[1][0]},${points[1][1]}`;

  let d = `M ${points[0][0]},${points[0][1]}`;
  for (let i = 1; i < points.length - 1; i++) {
    const [px, py] = points[i - 1];
    const [cx, cy] = points[i];
    const [nx, ny] = points[i + 1];
    const d1 = Math.hypot(cx - px, cy - py);
    const d2 = Math.hypot(nx - cx, ny - cy);
    const ra = Math.min(radius, d1 / 2, d2 / 2);
    if (ra < 0.5) {
      d += ` L ${cx},${cy}`;
    } else {
      const a = [cx - Math.sign(cx - px) * ra, cy - Math.sign(cy - py) * ra];
      const b = [cx + Math.sign(nx - cx) * ra, cy + Math.sign(ny - cy) * ra];
      d += ` L ${a[0]},${a[1]} Q ${cx},${cy} ${b[0]},${b[1]}`;
    }
  }
  const last = points[points.length - 1];
  d += ` L ${last[0]},${last[1]}`;
  return d;
}

/** Sample a cubic bezier curve into equidistant points along t in [0, 1]. */
export function sampleCubicBezier(
  p0: readonly [number, number],
  p1: readonly [number, number],
  p2: readonly [number, number],
  p3: readonly [number, number],
  samples = 20,
): [number, number][] {
  const result: [number, number][] = [];
  for (let i = 0; i <= samples; i++) {
    const t = i / samples;
    const mt = 1 - t;
    const x = mt ** 3 * p0[0] + 3 * mt ** 2 * t * p1[0] + 3 * mt * t ** 2 * p2[0] + t ** 3 * p3[0];
    const y = mt ** 3 * p0[1] + 3 * mt ** 2 * t * p1[1] + 3 * mt * t ** 2 * p2[1] + t ** 3 * p3[1];
    result.push([x, y]);
  }
  return result;
}

const DIR: Record<Side, [number, number]> = {
  right: [1, 0],
  left: [-1, 0],
  top: [0, -1],
  bottom: [0, 1],
};

const PERP: Record<Side, [number, number]> = {
  right: [0, 1],
  left: [0, 1],
  top: [1, 0],
  bottom: [1, 0],
};

/**
 * Route all wires on the canvas in one deterministic pass:
 * 1. Forward signal wires get dedicated cubic beziers where fan-out and fan-in wires diverge
 *    immediately near ports and never cross each other unnecessarily.
 * 2. Back wires (loops) are assigned distinct horizontal tracks below all intermediate obstacles
 *    and nested so shorter loops sit closer to blocks without overlapping.
 * 3. Step wires share clean orthogonal channels with distinct vertical stems.
 */
export function routeWires(
  wires: readonly RouteInput[],
  obstacles: readonly Obstacle[] = [],
): Map<string, WireRoute> {
  const results = new Map<string, WireRoute>();
  if (wires.length === 0) return results;

  const isBack = (w: RouteInput) =>
    (w.sourcePosition ?? "right") === "right" &&
    (w.targetPosition ?? "left") === "left" &&
    w.tx < w.sx + 8;

  const backWires: RouteInput[] = [];
  const forwardStepWires: RouteInput[] = [];
  const forwardSignalWires: RouteInput[] = [];

  for (const w of wires) {
    if (isBack(w)) {
      backWires.push(w);
    } else if (w.step) {
      forwardStepWires.push(w);
    } else {
      forwardSignalWires.push(w);
    }
  }

  // --- Back-wires (loops) ---
  if (backWires.length > 0) {
    // Sort loops by horizontal span: inner (shorter) loops get lower lanes (closer to blocks).
    const sortedLoops = [...backWires].sort(
      (a, b) =>
        (a.sx - a.tx) - (b.sx - b.tx) ||
        a.sx - b.sx ||
        byText(a.source, b.source) ||
        byText(a.id, b.id),
    );
    const loopLaneMap = new Map(sortedLoops.map((w, lane) => [w.id, lane]));

    // Exit lanes per source handle
    const bySource = byPort(backWires, "source");
    const sourceExitLaneMap = new Map<string, number>();
    for (const list of bySource.values()) {
      list.sort((a, b) => a.ty - b.ty || byText(a.id, b.id));
      list.forEach((w, i) => sourceExitLaneMap.set(w.id, i));
    }

    // Exit lanes per target handle
    const byTarget = byPort(backWires, "target");
    const targetExitLaneMap = new Map<string, number>();
    for (const list of byTarget.values()) {
      list.sort((a, b) => a.sy - b.sy || byText(a.id, b.id));
      list.forEach((w, i) => targetExitLaneMap.set(w.id, i));
    }

    for (const w of backWires) {
      const loopLane = loopLaneMap.get(w.id) ?? 0;
      const sLane = sourceExitLaneMap.get(w.id) ?? 0;
      const tLane = targetExitLaneMap.get(w.id) ?? 0;

      const exitSource = backWireExitDistance(sLane);
      const exitTarget = backWireExitDistance(tLane);

      // Find obstacles whose horizontal bounds overlap [tx - exitTarget - 4, sx + exitSource + 4]
      const spanMin = w.tx - exitTarget - 4;
      const spanMax = w.sx + exitSource + 4;
      const obstaclesInSpan = obstacles.filter((o) => o.x + o.width >= spanMin && o.x <= spanMax);
      const maxObstacleBottom = obstaclesInSpan.length
        ? Math.max(...obstaclesInSpan.map((o) => o.y + o.height))
        : -Infinity;

      const baseBelow = Math.max(w.sy, w.ty, maxObstacleBottom) + 28;
      const below = baseBelow + backWireLaneOffset(loopLane);

      const pts: [number, number][] = [
        [w.sx, w.sy],
        [w.sx + exitSource, w.sy],
        [w.sx + exitSource, below],
        [w.tx - exitTarget, below],
        [w.tx - exitTarget, w.ty],
        [w.tx, w.ty],
      ];
      const path = roundedPolyline(pts, 10);
      results.set(w.id, {
        id: w.id,
        kind: "back",
        path,
        labelX: (w.sx + w.tx) / 2,
        labelY: below,
      });
    }
  }

  // --- Forward step wires ---
  if (forwardStepWires.length > 0) {
    const byTarget = byPort(forwardStepWires, "target");

    for (const list of byTarget.values()) {
      list.sort((a, b) => a.sy - b.sy || byText(a.id, b.id));
      const count = list.length;
      list.forEach((w, lane) => {
        const midX = (w.sx + w.tx) / 2;
        const channelOffset = count > 1 ? (lane - (count - 1) / 2) * 12 : 0;
        const channelX = Math.max(w.sx + 16, Math.min(w.tx - 16, midX + channelOffset));
        const pts: [number, number][] = [
          [w.sx, w.sy],
          [channelX, w.sy],
          [channelX, w.ty],
          [w.tx, w.ty],
        ];
        const path = roundedPolyline(pts, 8);
        results.set(w.id, {
          id: w.id,
          kind: "step",
          path,
          labelX: channelX,
          labelY: (w.sy + w.ty) / 2,
        });
      });
    }
  }

  // --- Forward signal wires ---
  if (forwardSignalWires.length > 0) {
    const bySource = byPort(forwardSignalWires, "source");
    const sourceLaneMap = new Map<string, { lane: number; count: number }>();
    for (const list of bySource.values()) {
      list.sort((a, b) => a.ty - b.ty || byText(a.targetHandle ?? "", b.targetHandle ?? "") || byText(a.id, b.id));
      const count = list.length;
      list.forEach((w, lane) => sourceLaneMap.set(w.id, { lane, count }));
    }

    const byTarget = byPort(forwardSignalWires, "target");
    const targetLaneMap = new Map<string, { lane: number; count: number }>();
    for (const list of byTarget.values()) {
      list.sort((a, b) => a.sy - b.sy || byText(a.sourceHandle ?? "", b.sourceHandle ?? "") || byText(a.id, b.id));
      const count = list.length;
      list.forEach((w, lane) => targetLaneMap.set(w.id, { lane, count }));
    }

    for (const w of forwardSignalWires) {
      const sInfo = sourceLaneMap.get(w.id) ?? { lane: 0, count: 1 };
      const tInfo = targetLaneMap.get(w.id) ?? { lane: 0, count: 1 };

      const sPos = w.sourcePosition ?? "right";
      const tPos = w.targetPosition ?? "left";

      const dx = w.tx - w.sx;
      const dist = Math.max(20, Math.abs(dx));

      let h1 = Math.max(20, Math.min(dist * 0.75, dist * 0.45));
      let sPerpOffset = 0;
      if (sInfo.count > 1) {
        const sCenter = (sInfo.count - 1) / 2;
        const sLaneFrac = (sInfo.lane - sCenter);
        h1 = Math.max(24, Math.min(dist * 0.8, dist * (0.42 + 0.08 * sLaneFrac)));
        sPerpOffset = sLaneFrac * 6;
      }

      let h2 = Math.max(20, Math.min(dist * 0.75, dist * 0.45));
      let tPerpOffset = 0;
      if (tInfo.count > 1) {
        const tCenter = (tInfo.count - 1) / 2;
        const tLaneFrac = (tInfo.lane - tCenter);
        h2 = Math.max(24, Math.min(dist * 0.8, dist * (0.42 + 0.08 * tLaneFrac)));
        tPerpOffset = tLaneFrac * 6;
      }

      const sDir = DIR[sPos] ?? [1, 0];
      const sPerp = PERP[sPos] ?? [0, 1];
      const tDir = DIR[tPos] ?? [-1, 0];
      const tPerp = PERP[tPos] ?? [0, 1];

      const cx1 = w.sx + sDir[0] * h1 + sPerp[0] * sPerpOffset;
      const cy1 = w.sy + sDir[1] * h1 + sPerp[1] * sPerpOffset;
      const cx2 = w.tx + tDir[0] * h2 + tPerp[0] * tPerpOffset;
      const cy2 = w.ty + tDir[1] * h2 + tPerp[1] * tPerpOffset;

      const path = `M ${w.sx},${w.sy} C ${cx1},${cy1} ${cx2},${cy2} ${w.tx},${w.ty}`;
      const labelX = 0.125 * w.sx + 0.375 * cx1 + 0.375 * cx2 + 0.125 * w.tx;
      const labelY = 0.125 * w.sy + 0.375 * cy1 + 0.375 * cy2 + 0.125 * w.ty;

      results.set(w.id, {
        id: w.id,
        kind: "forward",
        path,
        labelX,
        labelY,
      });
    }
  }

  return results;
}
