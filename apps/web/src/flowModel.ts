// What the Flow canvas holds, and the model functions that make and mend it: blocks from the
// catalog, wires between their ports, and keeping both in step with the station and the catalog.

import { addEdge, MarkerType, type Connection, type Edge, type Node } from "@xyflow/react";
import { iconValid, type RotationDeg } from "@paperboat/shared";
import { iconEnd, inForm, signalEnd, type PortSides, type WireEnd } from "./ports.ts";
import type { GroupNode } from "./groups";
import type { BlockDef, Connectors, ObjectDef, ParamValue, Station } from "./protocol";
import type { WirePoint } from "./wireRouting.ts";

/**
 * `missing`: the backend's catalog has no block of this type (an old graph, another
 * backend's, a block renamed in code). The node keeps its type, params and wires, drawn
 * red from what it had, until the type comes back or the user replaces it.
 * `ports`: where the user moved its ports to (drag a port's name); absent: inputs left, outputs right.
 */
export type BlockData = {
  def: BlockDef;
  params: Record<string, ParamValue>;
  missing?: boolean;
  ports?: PortSides;
  rotation?: RotationDeg;
  mirror?: boolean;
  commented?: boolean;
  commentMode?: "out" | "through";
  comment?: string;
};
export type BlockNode = Node<BlockData, "block">;
/** What the Flow canvas holds: blocks, and the groups (frames) around some of them. */
export type FlowNode = BlockNode | GroupNode;
export const isBlock = (n: FlowNode): n is BlockNode => n.type === "block";
/**
 * `type`: the type of the ports it joins: `step` (control passes along it), `signal`, `image`…
 * `points`: the bends the user dragged it to, source end first; absent, it is routed.
 */
export type Wire = Edge<{ type: string; points?: WirePoint[] }, "wire">;

export { inForm };

/** A block's ports as they are now (a magic block's: its form's). */
export const portsOf = (n: BlockNode) => inForm(n.data.def, n.data.params);

export const portDef = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  def?.[side].find((p) => p.id === port);
/**
 * The end of a wire at port `port` of `def`: an icon connector (its class from the catalog, either
 * direction for an acausal one), else a legacy signal port on the `side` the wire runs from.
 */
export const wireEnd = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs", connectors: Connectors): WireEnd | null => {
  const ip = iconValid(def?.view?.icon) ? def?.view?.icon?.ports?.find((p) => p.id === port) : undefined;
  if (ip) return iconEnd(ip, connectors);
  return portDef(def, port, side) ? signalEnd(side === "inputs" ? "in" : "out") : null;
};

export const portType = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  portDef(def, port, side)?.type;

export const hasStepPorts = (def: BlockDef) => [...def.inputs, ...def.outputs].some((p) => p.type === "step");

export function makeNode(def: BlockDef, id: string, x: number, y: number, params: Record<string, ParamValue> = {}): BlockNode {
  const defaults = Object.fromEntries(def.params.map((p) => [p.id, p.default]));
  // A linked block goes when its station object goes, not from the Flow page.
  return { id, type: "block", position: { x, y }, deletable: !def.linked, data: { def, params: { ...defaults, ...params } } };
}

/**
 * Keep the linked blocks in step with the station: one per station object that spawns
 * one (same id), none for objects that are gone. Returns `nodes` itself when nothing changes.
 */
export function syncLinked(nodes: FlowNode[], station: Station | null, objects: ObjectDef[], blocks: BlockDef[]): FlowNode[] {
  const spawns = new Map(objects.flatMap((o) => (o.block ? [[o.type, o.block] as const] : [])));
  const defs = new Map(blocks.map((b) => [b.type, b]));
  const wanted = new Map(
    (station?.objects ?? []).flatMap((o) => {
      const def = defs.get(spawns.get(o.type) ?? "");
      return def ? [[o.id, def] as const] : [];
    }),
  );
  const kept = nodes.filter((n) => !isBlock(n) || !n.data.def.linked || wanted.has(n.id));
  const have = new Set(kept.map((n) => n.id));
  // New ones go below everything on the canvas, so they never cover a block.
  const top = kept.filter((n) => !n.parentId);
  const left = top.length ? Math.min(...top.map((n) => n.position.x)) : 0;
  const below = top.length ? Math.max(...top.map((n) => n.position.y + (n.measured?.height ?? 130))) + 60 : 0;
  const added = [...wanted]
    .filter(([id]) => !have.has(id))
    .map(([id, def], i) => makeNode(def, id, left + 220 * (i % 4), below + 150 * Math.floor(i / 4)));
  return kept.length === nodes.length && added.length === 0 ? nodes : [...kept, ...added];
}

export function makeWire(c: Connection, type: string): Wire {
  const step = type === "step";
  return {
    id: `${c.source}.${c.sourceHandle}-${c.target}.${c.targetHandle}`,
    type: "wire",
    ...c,
    data: { type },
    markerEnd: step ? { type: MarkerType.ArrowClosed } : undefined,
  };
}

/**
 * Add a wire, replacing the one it can't share a port with: a signal input reads one
 * wire, and a step output leads to one next block (a step input can be reached from many).
 */
export function connect(wires: Wire[], c: Connection, type: string): Wire[] {
  const step = type === "step";
  const clash = (w: Wire) =>
    step
      ? w.source === c.source && w.sourceHandle === c.sourceHandle
      : w.target === c.target && w.targetHandle === c.targetHandle;
  return addEdge(makeWire(c, type), wires.filter((w) => !clash(w)));
}

export function freeId(type: string, nodes: FlowNode[]): string {
  const taken = new Set(nodes.map((n) => n.id));
  let i = 1;
  while (taken.has(`${type}-${i}`)) i++;
  return `${type}-${i}`;
}

/**
 * A stand-in definition for a block whose type the catalog lacks, from what is known of
 * it: the ports its wires use (and any it had), and its params as they were.
 */
export function missingDef(
  type: string,
  params: Record<string, ParamValue>,
  wires: { source: string; sourceHandle?: string | null; target: string; targetHandle?: string | null; type?: string }[],
  id: string,
  had?: BlockDef,
): BlockDef {
  const ports = (side: "inputs" | "outputs") => {
    const known = new Map((had?.[side] ?? []).map((p) => [p.id, p.type]));
    for (const w of wires) {
      const port = side === "inputs" ? (w.target === id ? w.targetHandle : null) : w.source === id ? w.sourceHandle : null;
      if (port && !known.has(port)) known.set(port, w.type ?? "signal");
    }
    return [...known].map(([pid, ptype]) => ({ id: pid, type: ptype }));
  };
  return {
    type,
    label: type,
    kind: "function",
    inputs: ports("inputs"),
    outputs: ports("outputs"),
    params: Object.entries(params).map(([pid, v]) =>
      typeof v === "number"
        ? { id: pid, type: "number" as const, default: v }
        : typeof v === "boolean"
          ? { id: pid, type: "boolean" as const, default: v }
          : { id: pid, type: "text" as const, default: typeof v === "string" ? v : "var" in v ? `=${v.var}` : `=${v.expr}` },
    ),
  };
}

/**
 * The same canvas against a new catalog (the backend came back, perhaps changed): each
 * block takes its type's current definition, keeping the params it still has; a type that
 * is gone leaves a missing block; a missing one whose type is back is whole again.
 * Returns `nodes` itself when nothing changes.
 */
export function resolveBlocks(nodes: FlowNode[], edges: Wire[], blocks: BlockDef[]): FlowNode[] {
  const defs = new Map(blocks.map((b) => [b.type, b]));
  let changed = false;
  const out = nodes.map((n): FlowNode => {
    if (!isBlock(n)) return n;
    const def = defs.get(n.data.def.type);
    if (def === n.data.def || (!def && n.data.missing)) return n;
    changed = true;
    if (def) {
      const defaults = Object.fromEntries(def.params.map((p) => [p.id, p.default]));
      const kept = Object.fromEntries(Object.entries(n.data.params).filter(([k]) => k in defaults));
      const ports = n.data.ports ? { ports: n.data.ports } : {};
      const rotation = n.data.rotation !== undefined ? { rotation: n.data.rotation } : {};
      const mirror = n.data.mirror !== undefined ? { mirror: n.data.mirror } : {};
      const commented = n.data.commented !== undefined ? { commented: n.data.commented } : {};
      const commentMode = n.data.commentMode !== undefined ? { commentMode: n.data.commentMode } : {};
      const comment = n.data.comment !== undefined ? { comment: n.data.comment } : {};
      return { ...n, deletable: !def.linked, data: { def, params: { ...defaults, ...kept }, ...ports, ...rotation, ...mirror, ...commented, ...commentMode, ...comment } };
    }
    const wires = edges.map((e) => ({ ...e, type: e.data?.type }));
    return { ...n, deletable: true, data: { ...n.data, def: missingDef(n.data.def.type, n.data.params, wires, n.id, n.data.def), missing: true } };
  });
  return changed ? out : nodes;
}

/**
 * Wires to a block that's gone (its station object was removed), or to a port it no longer
 * has (a magic block switched form), go too. Returns `edges` itself when all stay.
 */
export function withoutLooseWires(nodes: FlowNode[], edges: Wire[]): Wire[] {
  const ports = new Map<string, Set<string> | null>(
    nodes.map((n) => [n.id, isBlock(n) && !n.data.missing ? new Set([...portsOf(n).inputs, ...portsOf(n).outputs].map((p) => p.id)) : null]),
  );
  const has = (id: string, port: string | null | undefined) => {
    if (!ports.has(id)) return false;
    const p = ports.get(id);
    return p === null || p === undefined || p.has(port ?? ""); // groups and missing blocks keep their wires
  };
  const kept = edges.filter((e) => has(e.source, e.sourceHandle) && has(e.target, e.targetHandle));
  return kept.length === edges.length ? edges : kept;
}
