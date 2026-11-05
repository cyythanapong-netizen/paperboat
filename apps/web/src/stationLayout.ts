import type { Station } from "./protocol";
import { layoutGraph, type LayoutEdge, type LayoutNode } from "./graphLayout.ts";

/**
 * The Station adapter for the shared graph layout. `nodes` supplies measured object sizes and
 * `edges` the station links. Returns `station` itself for a no-op; otherwise only `pos` changes.
 */
export function arrangeStationObjects(station: Station, nodes: readonly LayoutNode[], edges: readonly LayoutEdge[]): Station {
  if (station.objects.length === 0) return station;
  const positions = layoutGraph(nodes, edges);
  let changed = false;
  const objects = station.objects.map((o) => {
    const at = positions.get(o.id);
    if (!at || (o.pos?.[0] === at.x && o.pos[1] === at.y)) return o;
    changed = true;
    return { ...o, pos: [at.x, at.y] as [number, number] };
  });
  return changed ? { ...station, objects } : station;
}
