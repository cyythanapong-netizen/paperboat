export type LayoutPoint = { x: number; y: number };
export type LayoutNode = { id: string; width?: number; height?: number; pin?: "first" | "last" };
export type LayoutEdge = { source: string; target: string; sourceOffset?: number; targetOffset?: number };
export type LayoutOptions = { origin?: LayoutPoint; columnGap?: number; rowGap?: number };

const DEFAULT_WIDTH = 220;
const DEFAULT_HEIGHT = 130;
const DEFAULT_COLUMN_GAP = 80;
const DEFAULT_ROW_GAP = 40;

type Size = { width: number; height: number };

const byId = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);
const dimension = (value: number | undefined, fallback: number) =>
  value !== undefined && Number.isFinite(value) && value > 0 ? value : fallback;
const gap = (value: number | undefined, fallback: number) =>
  value !== undefined && Number.isFinite(value) && value >= 0 ? value : fallback;
const coordinate = (value: number | undefined) => (value !== undefined && Number.isFinite(value) ? value : 0);
const rounded = (value: number) => {
  const result = Math.round(value);
  return result === 0 ? 0 : result;
};

/**
 * Count wire crossings between all adjacent layer pairs.
 * Takes port offsets into account for precise visual crossings.
 */
export function countCrossings(
  columns: readonly (readonly string[])[],
  edges: readonly LayoutEdge[],
  yMap: ReadonlyMap<string, number>,
  sizes: ReadonlyMap<string, Size>,
): number {
  let total = 0;
  for (let c = 0; c < columns.length - 1; c++) {
    const leftCol = new Set(columns[c]);
    const rightCol = new Set(columns[c + 1]);
    const channelEdges: { y1: number; y2: number }[] = [];

    for (const e of edges) {
      if (leftCol.has(e.source) && rightCol.has(e.target)) {
        const sSize = sizes.get(e.source) ?? { width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT };
        const tSize = sizes.get(e.target) ?? { width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT };
        const y1 = (yMap.get(e.source) ?? 0) + (e.sourceOffset ?? sSize.height / 2);
        const y2 = (yMap.get(e.target) ?? 0) + (e.targetOffset ?? tSize.height / 2);
        channelEdges.push({ y1, y2 });
      }
    }

    for (let i = 0; i < channelEdges.length; i++) {
      for (let j = i + 1; j < channelEdges.length; j++) {
        const a = channelEdges[i];
        const b = channelEdges[j];
        if ((a.y1 < b.y1 && a.y2 > b.y2) || (a.y1 > b.y1 && a.y2 < b.y2)) {
          total++;
        }
      }
    }
  }
  return total;
}

/**
 * Return integral top-left positions for a left-to-right directed graph. Back edges found by a
 * deterministic depth-first traversal are left out of the layering graph, so cycles terminate
 * without special cases in the longest-path calculation.
 *
 * Wire crossings are minimized using iterative barycenter sweeps with transpose passes,
 * preserving port alignment so straight connections remain horizontal and long edges route
 * through dummy channels rather than clipping intermediate blocks.
 */
export function layoutGraph(
  nodes: readonly LayoutNode[],
  edges: readonly LayoutEdge[],
  options: LayoutOptions = {},
): Map<string, LayoutPoint> {
  const sizes = new Map<string, Size>();
  const pins = new Map<string, "first" | "last">();

  for (const node of nodes) {
    const size = {
      width: dimension(node.width, DEFAULT_WIDTH),
      height: dimension(node.height, DEFAULT_HEIGHT),
    };
    const previous = sizes.get(node.id);
    sizes.set(node.id, {
      width: Math.max(previous?.width ?? 0, size.width),
      height: Math.max(previous?.height ?? 0, size.height),
    });
    if (node.pin) pins.set(node.id, node.pin);
  }
  if (sizes.size === 0) return new Map();

  const originalIds = [...sizes.keys()].sort(byId);
  const ids = [...originalIds];

  const adjacency = new Map<string, Set<string>>(ids.map((id) => [id, new Set<string>()]));
  for (const edge of edges) {
    if (edge.source === edge.target || !sizes.has(edge.source) || !sizes.has(edge.target)) continue;
    adjacency.get(edge.source)!.add(edge.target);
  }
  const outgoing = new Map<string, string[]>();
  for (const id of ids) outgoing.set(id, [...adjacency.get(id)!].sort(byId));

  // Depth-first forest for deterministic topological sort & back-edge removal
  const state = new Map<string, "visiting" | "done">();
  const finished: string[] = [];
  const forward = new Map<string, string[]>(ids.map((id) => [id, []]));
  const backEdges: { source: string; target: string }[] = [];

  for (const root of ids) {
    if (state.has(root)) continue;
    state.set(root, "visiting");
    const stack = [{ id: root, next: 0 }];
    while (stack.length > 0) {
      const frame = stack[stack.length - 1];
      const targets = outgoing.get(frame.id)!;
      if (frame.next === targets.length) {
        state.set(frame.id, "done");
        finished.push(frame.id);
        stack.pop();
        continue;
      }
      const target = targets[frame.next++];
      if (state.get(target) === "visiting") {
        backEdges.push({ source: frame.id, target });
        continue;
      }
      forward.get(frame.id)!.push(target);
      if (state.get(target) === "done") continue;
      state.set(target, "visiting");
      stack.push({ id: target, next: 0 });
    }
  }
  const topological = finished.reverse();
  for (const targets of forward.values()) targets.sort(byId);

  // Longest-path layering
  const layerOf = new Map<string, number>(ids.map((id) => [id, 0]));
  for (const source of topological) {
    for (const target of forward.get(source)!) {
      layerOf.set(target, Math.max(layerOf.get(target)!, layerOf.get(source)! + 1));
    }
  }

  // Handle pins (e.g. subsystem inputs at layer 0, outputs at last layer)
  let maxLayer = Math.max(...layerOf.values(), 0);
  for (const [id, pin] of pins) {
    if (pin === "first") layerOf.set(id, 0);
    else if (pin === "last") layerOf.set(id, maxLayer);
  }
  maxLayer = Math.max(...layerOf.values(), 0);
  const layerCount = maxLayer + 1;

  // Insert dummy nodes for edges spanning > 1 layer
  type RoutedEdge = LayoutEdge & { isDummy?: boolean };
  const segmentedEdges: RoutedEdge[] = [];
  let dummyIndex = 0;

  for (const edge of edges) {
    if (edge.source === edge.target || !sizes.has(edge.source) || !sizes.has(edge.target)) continue;
    const l1 = layerOf.get(edge.source);
    const l2 = layerOf.get(edge.target);
    if (l1 === undefined || l2 === undefined) continue;

    if (l2 > l1 + 1) {
      let prevId = edge.source;
      let prevOffset = edge.sourceOffset;
      for (let l = l1 + 1; l < l2; l++) {
        const dummyId = `__dummy:${dummyIndex++}`;
        sizes.set(dummyId, { width: 0, height: 12 });
        layerOf.set(dummyId, l);
        ids.push(dummyId);

        segmentedEdges.push({
          source: prevId,
          target: dummyId,
          sourceOffset: prevOffset,
          targetOffset: 6,
          isDummy: true,
        });
        prevId = dummyId;
        prevOffset = 6;
      }
      segmentedEdges.push({
        source: prevId,
        target: edge.target,
        sourceOffset: 6,
        targetOffset: edge.targetOffset,
        isDummy: true,
      });
    } else if (l2 > l1) {
      segmentedEdges.push({ ...edge });
    }
  }

  // Initial column grouping
  let columns: string[][] = Array.from({ length: layerCount }, () => []);
  for (const id of ids) columns[layerOf.get(id)!].push(id);
  for (let c = 0; c < columns.length; c++) columns[c].sort(byId);

  const columnGap = gap(options.columnGap, DEFAULT_COLUMN_GAP);
  const rowGap = gap(options.rowGap, DEFAULT_ROW_GAP);
  const y = new Map<string, number>();

  // Directed adjacency for segmented edges
  const segIncoming = new Map<string, RoutedEdge[]>(ids.map((id) => [id, []]));
  const segOutgoing = new Map<string, RoutedEdge[]>(ids.map((id) => [id, []]));
  for (const e of segmentedEdges) {
    segOutgoing.get(e.source)?.push(e);
    segIncoming.get(e.target)?.push(e);
  }

  const placeColumn = (index: number) => {
    let top = 0;
    for (const id of columns[index]) {
      if (id.startsWith("__dummy:")) continue;
      y.set(id, top);
      top += Math.ceil(sizes.get(id)!.height + rowGap);
    }
    for (const id of columns[index]) {
      if (!id.startsWith("__dummy:")) continue;
      const inE = segIncoming.get(id);
      const outE = segOutgoing.get(id);
      const prevY = inE && inE[0] ? (y.get(inE[0].source) ?? 0) : top;
      const nextY = outE && outE[0] ? (y.get(outE[0].target) ?? 0) : top;
      y.set(id, Math.round((prevY + nextY) / 2));
    }
  };
  for (let index = 0; index < columns.length; index++) placeColumn(index);

  // Crossing minimization via barycenter sweeps & transpose passes
  const sweep = (forwardPass: boolean) => {
    let changed = false;
    const start = forwardPass ? 1 : columns.length - 2;
    const end = forwardPass ? columns.length : -1;
    const step = forwardPass ? 1 : -1;

    for (let index = start; index !== end; index += step) {
      const ranked = columns[index].map((id) => {
        const edgesList = forwardPass ? segIncoming.get(id)! : segOutgoing.get(id)!;
        const fallbackList = forwardPass ? segOutgoing.get(id)! : segIncoming.get(id)!;
        const activeList = edgesList.length > 0 ? edgesList : fallbackList;

        let barycenter = (y.get(id) ?? 0) + sizes.get(id)!.height / 2;
        if (activeList.length > 0) {
          let sum = 0;
          let totalWeight = 0;
          for (const e of activeList) {
            const neighbour = forwardPass ? e.source : e.target;
            const nY = y.get(neighbour) ?? 0;
            const nSize = sizes.get(neighbour)!;
            const weight = e.isDummy ? 1 : 2;
            const targetY = forwardPass
              ? nY + (e.sourceOffset ?? nSize.height / 2) - (e.targetOffset ?? sizes.get(id)!.height / 2)
              : nY + (e.targetOffset ?? nSize.height / 2) - (e.sourceOffset ?? sizes.get(id)!.height / 2);
            sum += targetY * weight;
            totalWeight += weight;
          }
          barycenter = sum / totalWeight;
        }
        return { id, barycenter };
      });

      ranked.sort((a, b) => a.barycenter - b.barycenter || byId(a.id, b.id));
      const ordered = ranked.map((entry) => entry.id);
      if (ordered.some((id, at) => id !== columns[index][at])) changed = true;
      columns[index] = ordered;
      placeColumn(index);
    }
    return changed;
  };

  let bestColumns = columns.map((col) => [...col]);
  let minCrossings = countCrossings(columns, segmentedEdges, y, sizes);

  for (let round = 0; round < 6 && minCrossings > 0; round++) {
    sweep(true);
    sweep(false);

    // Transpose pass: test swapping adjacent pairs in each column
    for (let c = 0; c < columns.length; c++) {
      for (let i = 0; i < columns[c].length - 1; i++) {
        const u = columns[c][i];
        const v = columns[c][i + 1];
        columns[c][i] = v;
        columns[c][i + 1] = u;
        placeColumn(c);
        const cr = countCrossings(columns, segmentedEdges, y, sizes);
        if (cr < minCrossings) {
          minCrossings = cr;
          bestColumns = columns.map((col) => [...col]);
        } else {
          // Revert swap
          columns[c][i] = u;
          columns[c][i + 1] = v;
          placeColumn(c);
        }
      }
    }

    const currentCr = countCrossings(columns, segmentedEdges, y, sizes);
    if (currentCr < minCrossings) {
      minCrossings = currentCr;
      bestColumns = columns.map((col) => [...col]);
    }
  }

  columns = bestColumns.map((col) => [...col]);
  for (let c = 0; c < columns.length; c++) placeColumn(c);

  // Vertical coordinate alignment: downstream columns align to predecessors
  for (let round = 0; round < 4; round++) {
    for (let c = 1; c < columns.length; c++) {
      const realInCol = columns[c].filter((id) => !id.startsWith("__dummy:"));
      if (realInCol.length === 0) continue;

      const desiredY = new Map<string, number>();
      for (const id of realInCol) {
        const inEdges = segIncoming.get(id) ?? [];
        const outEdges = segOutgoing.get(id) ?? [];

        if (inEdges.length > 0) {
          let sum = 0;
          let weightSum = 0;
          for (const e of inEdges) {
            const nSize = sizes.get(e.source)!;
            const targetY = (y.get(e.source) ?? 0) + (e.sourceOffset ?? nSize.height / 2) - (e.targetOffset ?? sizes.get(id)!.height / 2);
            const w = e.isDummy ? 1 : 4;
            sum += targetY * w;
            weightSum += w;
          }
          desiredY.set(id, sum / weightSum);
        } else if (outEdges.length > 0) {
          let sum = 0;
          let weightSum = 0;
          for (const e of outEdges) {
            const nSize = sizes.get(e.target)!;
            const targetY = (y.get(e.target) ?? 0) + (e.targetOffset ?? nSize.height / 2) - (e.sourceOffset ?? sizes.get(id)!.height / 2);
            const w = e.isDummy ? 1 : 4;
            sum += targetY * w;
            weightSum += w;
          }
          desiredY.set(id, sum / weightSum);
        } else {
          desiredY.set(id, y.get(id) ?? 0);
        }
      }

      // Order-preserving 1-D placement with ceiling of dimensions
      const placed: number[] = [Math.max(0, Math.round(desiredY.get(realInCol[0])!))];
      for (let k = 1; k < realInCol.length; k++) {
        const minY = placed[k - 1] + Math.ceil(sizes.get(realInCol[k - 1])!.height + rowGap);
        placed.push(Math.max(Math.round(desiredY.get(realInCol[k])!), minY));
      }
      for (let k = realInCol.length - 2; k >= 0; k--) {
        const maxY = placed[k + 1] - Math.ceil(sizes.get(realInCol[k])!.height + rowGap);
        placed[k] = Math.min(placed[k], maxY);
      }
      const shift = Math.min(0, ...placed);
      for (let k = 0; k < realInCol.length; k++) {
        y.set(realInCol[k], Math.round(placed[k] - shift));
      }
    }
  }

  // Adaptive column gaps based on channel wire density and vertical bending
  const raw = new Map<string, LayoutPoint>();
  let left = 0;
  for (let c = 0; c < columns.length; c++) {
    const col = columns[c];
    const width = Math.max(0, ...col.map((id) => sizes.get(id)!.width));
    for (const id of col) raw.set(id, { x: left, y: y.get(id)! });

    if (c < columns.length - 1) {
      const leftCol = new Set(col);
      const rightCol = new Set(columns[c + 1]);
      let bendingWires = 0;
      for (const e of segmentedEdges) {
        if (leftCol.has(e.source) && rightCol.has(e.target)) {
          const sY = (y.get(e.source) ?? 0) + (e.sourceOffset ?? sizes.get(e.source)!.height / 2);
          const tY = (y.get(e.target) ?? 0) + (e.targetOffset ?? sizes.get(e.target)!.height / 2);
          if (Math.abs(sY - tY) > 6) bendingWires++;
        }
      }
      const actualGap =
        options.columnGap !== undefined
          ? columnGap
          : Math.max(columnGap, 40 + Math.min(80, 8 * bendingWires));
      left += Math.ceil(width + actualGap);
    }
  }

  // Normalize origin
  const minX = Math.min(...originalIds.map((id) => raw.get(id)!.x));
  const minY = Math.min(...originalIds.map((id) => raw.get(id)!.y));
  const origin = { x: coordinate(options.origin?.x), y: coordinate(options.origin?.y) };

  return new Map(
    originalIds.map((id) => {
      const point = raw.get(id)!;
      return [id, { x: rounded(point.x - minX + origin.x), y: rounded(point.y - minY + origin.y) }];
    }),
  );
}
