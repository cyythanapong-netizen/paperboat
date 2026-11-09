// Following wires (#55): hovering a wire, a port or a block lights its wires and fades the rest.

import { useState } from "react";
import type { Wire } from "./flowModel";

export function useLitWires(edges: Wire[]) {
  // Hovering a wire, a port or a block lights its wires and fades the rest.
  const [lit, setLit] = useState<ReadonlySet<string> | null>(null);
  const follow = (e: { target: EventTarget | null }) => {
    const t = e.target as Element | null;
    const handle = t?.closest?.(".react-flow__handle");
    const node = t?.closest?.(".react-flow__node");
    const wire = t?.closest?.(".react-flow__edge");
    let ids: string[] | null = null;
    if (handle) {
      const n = handle.getAttribute("data-nodeid");
      const h = handle.getAttribute("data-handleid");
      ids = edges.filter((w) => (w.source === n && w.sourceHandle === h) || (w.target === n && w.targetHandle === h)).map((w) => w.id);
    } else if (wire) ids = [wire.getAttribute("data-id") ?? ""];
    else if (node) {
      const n = node.getAttribute("data-id");
      ids = edges.filter((w) => w.source === n || w.target === n).map((w) => w.id);
    }
    setLit((prev) => {
      if (!ids?.length) return prev === null ? prev : null;
      return prev && prev.size === ids.length && ids.every((i) => prev.has(i)) ? prev : new Set(ids);
    });
  };
  return { lit, follow, unlit: () => setLit(null) };
}
