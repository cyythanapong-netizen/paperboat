// Cards dropped from the browser onto the Flow canvas (#50): while one is dragged, plan what
// dropping it here would do (insert it into a wire, connect it to a near port, or just place it),
// show that, and do it on drop.

import { useState, type DragEvent, type RefObject, type Dispatch, type SetStateAction } from "react";
import type { XYPosition } from "@xyflow/react";
import { DRAG_TYPE, dragging, type Item } from "./Browser";
import { wireMismatch } from "./ports";
import type { BlockDef, PortDef } from "./protocol";
import { connect, freeId, makeNode, portDef, type FlowNode, type Wire } from "./flowModel";
import { inLevel } from "./flowGraph";

const typeOf = (p: PortDef) => p.type ?? "signal";
/** About a block's size, for where a new one goes under the pointer. */
export const BLOCK_SIZE = { w: 156, h: 70 };

/** What dropping a card here would do. */
export type Plan =
  | { kind: "place" }
  | { kind: "insert"; wire: Wire; input: PortDef; output: PortDef }
  | { kind: "port"; node: string; handle: string; side: "source" | "target"; port: PortDef; hx: number; hy: number };

export type Hint = { plan: Plan; x: number; y: number; label: string };

export function useCardDrop(
  wrap: RefObject<HTMLDivElement | null>,
  nodes: FlowNode[],
  edges: Wire[],
  level: string | null,
  defOf: (id: string) => BlockDef | undefined,
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
  screenToFlowPosition: (at: XYPosition) => XYPosition,
) {
  const [hint, setHint] = useState<Hint | null>(null);
  const plan = (def: BlockDef, x: number, y: number): Plan => {
    // On a wire whose type the block both takes and gives: insert it.
    const hit = document.elementsFromPoint(x, y).find((el) => el instanceof SVGElement && el.dataset.wire);
    const wire = hit ? edges.find((w) => w.id === (hit as SVGElement).dataset.wire) : undefined;
    if (wire) {
      const from = portDef(defOf(wire.source), wire.sourceHandle, "outputs");
      const to = portDef(defOf(wire.target), wire.targetHandle, "inputs");
      const input = from && def.inputs.find((q) => !wireMismatch(from, q));
      const output = to && def.outputs.find((q) => !wireMismatch(q, to));
      if (input && output) return { kind: "insert", wire, input, output };
    }
    // Near a free port of a type the block has: connect to it.
    let best: { plan: Plan; d: number } | null = null;
    for (const el of wrap.current?.querySelectorAll<HTMLElement>(".react-flow__handle") ?? []) {
      const node = el.dataset.nodeid ?? "";
      const handle = el.dataset.handleid ?? "";
      if (!node || !handle || handle.includes("::")) continue;
      const r = el.getBoundingClientRect();
      const hx = r.left + r.width / 2;
      const hy = r.top + r.height / 2;
      const d = Math.hypot(hx - x, hy - y);
      if (d > 56 || (best && best.d <= d)) continue;
      const side = el.classList.contains("source") ? "source" : "target";
      const theirs = portDef(defOf(node), handle, side === "source" ? "outputs" : "inputs");
      if (!theirs) continue;
      const t = typeOf(theirs);
      const port = side === "source" ? def.inputs.find((q) => !wireMismatch(theirs, q)) : def.outputs.find((q) => !wireMismatch(q, theirs));
      const taken = side === "target" && t !== "step" && edges.some((w) => w.target === node && w.targetHandle === handle);
      if (port && !taken) best = { plan: { kind: "port", node, handle, side, port, hx, hy }, d };
    }
    return best?.plan ?? { kind: "place" };
  };
  const describe = (def: BlockDef, pl: Plan) =>
    pl.kind === "insert"
      ? `Insert ${def.label}: ${pl.wire.source}.${pl.wire.sourceHandle} → ${pl.input.id}, ${pl.output.id} → ${pl.wire.target}.${pl.wire.targetHandle}`
      : pl.kind === "port"
        ? pl.side === "source"
          ? `Connect ${pl.node}.${pl.handle} → ${pl.port.id}`
          : `Connect ${pl.port.id} → ${pl.node}.${pl.handle}`
        : `Place ${def.label} here`;
  const ours = (e: DragEvent) => e.dataTransfer.types.includes(DRAG_TYPE) && dragging.current?.kind === "block";
  const onDragOver = (e: DragEvent) => {
    if (!ours(e)) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    const def = (dragging.current as Item & { kind: "block" }).def;
    const pl = plan(def, e.clientX, e.clientY);
    setHint({ plan: pl, x: e.clientX, y: e.clientY, label: describe(def, pl) });
  };
  const onDrop = (e: DragEvent) => {
    if (!ours(e)) return;
    e.preventDefault();
    const def = (dragging.current as Item & { kind: "block" }).def;
    dragging.current = null;
    document.body.classList.remove("dragging-card");
    setHint(null);
    const pl = plan(def, e.clientX, e.clientY);
    const at = screenToFlowPosition({ x: e.clientX, y: e.clientY });
    const id = freeId(def.type, nodes);
    const node = { ...inLevel(makeNode(def, id, Math.round(at.x - BLOCK_SIZE.w / 2), Math.round(at.y - 16)), level, nodes), selected: true };
    setNodes((ns) => [...ns.map((n) => (n.selected ? { ...n, selected: false } : n)), node]);
    if (pl.kind === "insert") {
      const w = pl.wire;
      const t = w.data?.type ?? "signal";
      setEdges((ws) => {
        const rest = ws.filter((x) => x.id !== w.id);
        const a = connect(rest, { source: w.source, sourceHandle: w.sourceHandle ?? null, target: id, targetHandle: pl.input.id }, t);
        return connect(a, { source: id, sourceHandle: pl.output.id, target: w.target, targetHandle: w.targetHandle ?? null }, t);
      });
    } else if (pl.kind === "port") {
      const t = typeOf(pl.port);
      const c =
        pl.side === "source"
          ? { source: pl.node, sourceHandle: pl.handle, target: id, targetHandle: pl.port.id }
          : { source: id, sourceHandle: pl.port.id, target: pl.node, targetHandle: pl.handle };
      setEdges((ws) => connect(ws, c, t));
    }
  };
  return { hint, setHint, onDragOver, onDrop };
}
