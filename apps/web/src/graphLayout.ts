export type LayoutPoint = { x: number; y: number };
export type LayoutNode = { id: string; width?: number; height?: number };
export type LayoutEdge = { source: string; target: string };
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
 * Return integral top-left positions for a left-to-right directed graph. Back edges found by a
 * deterministic depth-first traversal are left out of the layering graph, so cycles terminate
 * without special cases in the longest-path calculation.
 */
export function layoutGraph(
  nodes: readonly LayoutNode[],
  edges: readonly LayoutEdge[],
  options: LayoutOptions = {},
): Map<string, LayoutPoint> {
  // Besides tolerating repeated node records, taking the largest size makes even that case
  // independent of their order in the input array.
  const sizes = new Map<string, Size>();
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
  }
  if (sizes.size === 0) return new Map();

  const ids = [...sizes.keys()].sort(byId);
  const adjacency = new Map<string, Set<string>>(ids.map((id) => [id, new Set<string>()]));
  for (const edge of edges) {
    if (edge.source === edge.target || !sizes.has(edge.source) || !sizes.has(edge.target)) continue;
    adjacency.get(edge.source)!.add(edge.target);
  }
  const outgoing = new Map<string, string[]>();
  for (const id of ids) outgoing.set(id, [...adjacency.get(id)!].sort(byId));

  // A depth-first forest supplies both the back-edge filtering and a topological order. The
  // iterative form also keeps a long chain from consuming the JavaScript call stack.
  const state = new Map<string, "visiting" | "done">();
  const finished: string[] = [];
  const forward = new Map<string, string[]>(ids.map((id) => [id, []]));
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
      if (state.get(target) === "visiting") continue; // a back edge
      forward.get(frame.id)!.push(target);
      if (state.get(target) === "done") continue;
      state.set(target, "visiting");
      stack.push({ id: target, next: 0 });
    }
  }
  const topological = finished.reverse();
  for (const targets of forward.values()) targets.sort(byId);

  const incoming = new Map<string, string[]>(ids.map((id) => [id, []]));
  for (const [source, targets] of forward) for (const target of targets) incoming.get(target)!.push(source);
  for (const sources of incoming.values()) sources.sort(byId);

  // Every retained edge advances at least one layer. Taking the maximum predecessor layer is
  // therefore the longest-path layering, including when a shorter edge bypasses a layer.
  const layerOf = new Map<string, number>(ids.map((id) => [id, 0]));
  for (const source of topological) {
    for (const target of forward.get(source)!) {
      layerOf.set(target, Math.max(layerOf.get(target)!, layerOf.get(source)! + 1));
    }
  }
  const layerCount = Math.max(...layerOf.values()) + 1;
  const columns: string[][] = Array.from({ length: layerCount }, () => []);
  for (const id of ids) columns[layerOf.get(id)!].push(id);

  const columnGap = gap(options.columnGap, DEFAULT_COLUMN_GAP);
  const rowGap = gap(options.rowGap, DEFAULT_ROW_GAP);
  const y = new Map<string, number>();
  const centers = new Map<string, number>();
  const placeColumn = (index: number) => {
    let top = 0;
    for (const id of columns[index]) {
      y.set(id, top);
      centers.set(id, top + sizes.get(id)!.height / 2);
      // Round strides up rather than positions down: fractional dimensions can never make two
      // integral rectangles overlap after the final origin translation and rounding.
      top += Math.ceil(sizes.get(id)!.height + rowGap);
    }
  };
  for (let index = 0; index < columns.length; index++) placeColumn(index);

  // Alternate predecessor and successor barycenters. Using actual neighbour centers accounts for
  // different row heights; IDs, rather than input order or the previous sort, break every tie.
  const sweep = (forwardPass: boolean) => {
    let changed = false;
    const start = forwardPass ? 1 : columns.length - 2;
    const end = forwardPass ? columns.length : -1;
    const step = forwardPass ? 1 : -1;
    for (let index = start; index !== end; index += step) {
      const primary = forwardPass ? incoming : forward;
      const secondary = forwardPass ? forward : incoming;
      const ranked = columns[index].map((id) => {
        const preferred = primary.get(id)!;
        const neighbours = preferred.length > 0 ? preferred : secondary.get(id)!;
        const barycenter = neighbours.length
          ? neighbours.reduce((sum, neighbour) => sum + centers.get(neighbour)!, 0) / neighbours.length
          : centers.get(id)!;
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
  for (let round = 0; round < 2 && columns.length > 1; round++) {
    const forwardChanged = sweep(true);
    const backwardChanged = sweep(false);
    if (!forwardChanged && !backwardChanged) break;
  }

  const raw = new Map<string, LayoutPoint>();
  let left = 0;
  for (const column of columns) {
    const width = Math.max(...column.map((id) => sizes.get(id)!.width));
    for (const id of column) raw.set(id, { x: left, y: y.get(id)! });
    left += Math.ceil(width + columnGap);
  }

  // Explicit normalization keeps the origin contract true even if the placement passes above are
  // changed later to use centered or otherwise negative working coordinates.
  const minX = Math.min(...[...raw.values()].map((point) => point.x));
  const minY = Math.min(...[...raw.values()].map((point) => point.y));
  const origin = { x: coordinate(options.origin?.x), y: coordinate(options.origin?.y) };
  return new Map(
    ids.map((id) => {
      const point = raw.get(id)!;
      return [id, { x: rounded(point.x - minX + origin.x), y: rounded(point.y - minY + origin.y) }];
    }),
  );
}
