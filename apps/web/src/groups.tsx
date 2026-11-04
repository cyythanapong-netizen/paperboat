import { NodeResizer, Position, type Edge, type Node, type NodeProps } from "@xyflow/react";
import { createContext, useContext } from "react";
import type { BlockDef, GraphGroup, PortDef } from "./protocol";
import type { PortSides } from "./ports";
import { usePortLayout } from "./portLayout";
import { PortHandle } from "./wireSnap";

import { NumberField } from "./NumberField";
/**
 * Groups on the Flow page (#68). Three kinds:
 * - **subsystem**: a block you enter and leave, like a Simulink subsystem. Outside it is one
 *   block whose ports are the wires crossing its edge; double-click opens its own level,
 *   where the wires from outside come in through **In** and leave through **Out** ports.
 * - **story**: the same, and one step of a Step flow (ports in → next). When a path arrives,
 *   its blocks run once per clock step for `seconds` (0: once); then the path moves on, and
 *   what the story computed stays on its outputs for the next story to read.
 * - **visual** (a frame): a dashed box that tidies the canvas; its blocks run as if ungrouped.
 *   Collapsed, it shows as one block, as a subsystem does from outside.
 * Subsystems nest (a group's `parentId` is the group it is in). Backends see a flat graph.
 */
export type GroupData = {
  kind: "visual" | "subsystem" | "story";
  label: string;
  /** A frame drawn as one block (subsystems and stories always are, from outside). */
  collapsed: boolean;
  seconds: number;
  /** A story's blocks start afresh each time a path arrives. */
  reset: boolean;
  /** A frame's size when open. */
  size: { w: number; h: number };
  /** Where the user put its ports (as a block's `ports`). */
  sides?: PortSides;
  /** Filled for display: the ports it shows as a block (`node::port` ids). */
  ports?: { inputs: PortDef[]; outputs: PortDef[] };
};
export type GroupNode = Node<GroupData, "frame">;

/** The step ports a story has, as a block definition. */
export const STORY_DEF: BlockDef = {
  type: "story",
  label: "Story",
  kind: "function",
  inputs: [{ id: "in", type: "step" }],
  outputs: [{ id: "next", type: "step" }],
  params: [],
};

/** A group's handle for a port inside it: `node::port`. */
export const SEP = "::";
export const portHandle = (node: string, port: string) => `${node}${SEP}${port}`;
export const splitHandle = (handle: string): [string, string] | null => {
  const i = handle.indexOf(SEP);
  return i < 0 ? null : [handle.slice(0, i), handle.slice(i + SEP.length)];
};

export const isFrame = (n: Node): n is GroupNode => n.type === "frame";
/** A group that is a level of its own: a subsystem or a story. */
export const isLevel = (n: Node | undefined): n is GroupNode => !!n && isFrame(n) && n.data.kind !== "visual";

/** Actions a group offers, provided by the Flow editor. */
export const GroupActions = createContext<{
  update: (id: string, change: Partial<GroupData>) => void;
  ungroup: (id: string) => void;
  /** Open a subsystem or story: show its level. */
  enter: (id: string) => void;
  /** What is happening inside a group: its most urgent block's state. */
  stateOf: (id: string) => "failed" | "running" | undefined;
}>({ update: () => {}, ungroup: () => {}, enter: () => {}, stateOf: () => undefined });

const HEAD = 34;

export function GroupBox(props: NodeProps<GroupNode>) {
  return props.data.kind === "visual" ? <Frame {...props} /> : <Subsystem {...props} />;
}

/** A frame: a dashed box round its blocks, or one block when collapsed. */
function Frame({ id, data, selected }: NodeProps<GroupNode>) {
  const { update, ungroup } = useContext(GroupActions);
  const ports = data.ports ?? { inputs: [], outputs: [] };
  return (
    <div className={`frame frame-visual${data.collapsed ? " collapsed" : ""}`}>
      {!data.collapsed && (
        <NodeResizer
          isVisible={selected}
          minWidth={180}
          minHeight={100}
          onResizeEnd={(_, p) => update(id, { size: { w: p.width, h: p.height } })}
        />
      )}
      <div className="frame-head" style={{ height: HEAD }}>
        <button className="frame-toggle nodrag" title={data.collapsed ? "Open" : "Collapse"} onClick={() => update(id, { collapsed: !data.collapsed })}>
          {data.collapsed ? "▸" : "▾"}
        </button>
        <span className="frame-kind kind-visual">frame</span>
        <input className="frame-label nodrag" value={data.label} onChange={(e) => update(id, { label: e.target.value })} />
        <button className="remove nodrag" title="Remove the frame (keeps the blocks)" onClick={() => ungroup(id)}>
          ⊟
        </button>
      </div>
      {data.collapsed && (
        <div className="frame-ports">
          {ports.inputs.map((p) => (
            <div key={p.id} className={`port in port-${p.type}`}>
              <PortHandle type="target" position={Position.Left} id={p.id} />
              {p.id.replace(SEP, ".")}
            </div>
          ))}
          {ports.outputs.map((p) => (
            <div key={p.id} className={`port out port-${p.type}`}>
              {p.id.replace(SEP, ".")}
              <PortHandle type="source" position={Position.Right} id={p.id} />
            </div>
          ))}
          {ports.inputs.length + ports.outputs.length === 0 && <div className="muted frame-empty">no wires cross it</div>}
        </div>
      )}
    </div>
  );
}

/** A subsystem or story from outside: one block, its ports movable like a block's. */
function Subsystem({ id, data, selected }: NodeProps<GroupNode>) {
  const { update, ungroup, enter, stateOf } = useContext(GroupActions);
  const story = data.kind === "story";
  const inputs = [...(story ? STORY_DEF.inputs : []), ...(data.ports?.inputs ?? [])];
  const outputs = [...(story ? STORY_DEF.outputs : []), ...(data.ports?.outputs ?? [])];
  const ports = usePortLayout({
    id,
    def: { inputs, outputs },
    sides: data.sides,
    save: (sides) => update(id, { sides }),
    name: (p) => p.id.replace(SEP, "."),
  });
  const state = stateOf(id);
  const tag = state ?? (story ? "story" : "subsystem");
  const classes = ["block", "subsystem", story && "step", selected && "selected", state === "running" && "active", state === "failed" && "failed"];
  return (
    <div
      className={classes.filter(Boolean).join(" ")}
      title="Double-click to open"
      onDoubleClick={(e) => {
        if ((e.target as HTMLElement).closest("input, button, .port")) return;
        e.stopPropagation();
        enter(id);
      }}
    >
      {ports.top}
      <div className="block-title">
        <span className="block-name">{data.label}</span>
        <span className={`block-tag tag-${tag}`}>{tag}</span>
        <button className="fold nodrag" title="Open: show what is inside (double-click)" onClick={() => enter(id)}>
          open
        </button>
        <button className="remove nodrag" title="Ungroup: put its blocks back here" onClick={() => ungroup(id)}>
          ⊟
        </button>
      </div>
      {ports.rows}
      {inputs.length + outputs.length === 0 && <div className="muted frame-empty">no wires cross it</div>}
      {story && (
        <>
          <label className="param" title="How long the story runs when a path arrives (0: once)">
            <span>
              seconds<span className="unit"> s</span>
            </span>
            <NumberField className="nodrag num" value={data.seconds} valid={(v) => v >= 0} onChange={(seconds) => update(id, { seconds })} />
          </label>
          <label className="param" title="Start its blocks afresh each time a path arrives (a story in a loop starts over)">
            <span>fresh</span>
            <input className="nodrag" type="checkbox" checked={data.reset} onChange={(e) => update(id, { reset: e.target.checked })} />
          </label>
        </>
      )}
      {ports.bottom}
    </div>
  );
}

/** Where the wires from outside a level come in (In) and leave (Out), at its edges. */
export type BoundaryData = { dir: "in" | "out"; label: string; type: string; handle: string };
export type BoundaryNode = Node<BoundaryData, "boundary">;

export function Boundary({ data }: NodeProps<BoundaryNode>) {
  const into = data.dir === "in";
  return (
    <div className={`boundary boundary-${data.dir} port-${data.type}`} title={into ? `Comes in from ${data.label}, outside` : `Goes out to ${data.label}, outside`}>
      {!into && <PortHandle type="target" position={Position.Left} id={data.handle} />}
      <span className="boundary-tag">{into ? "In" : "Out"}</span>
      <span className="boundary-name">{data.label}</span>
      {into && <PortHandle type="source" position={Position.Right} id={data.handle} />}
    </div>
  );
}

type AnyNode = Node & { data: unknown };
type ById = Map<string, AnyNode>;
const byIdOf = (nodes: AnyNode[]): ById => new Map(nodes.map((n) => [n.id, n]));

/** Where a node is on the canvas, through every group it is in. */
export function absolute(n: Node, nodes: Node[] | ById): { x: number; y: number } {
  const byId = nodes instanceof Map ? nodes : byIdOf(nodes as AnyNode[]);
  let { x, y } = n.position;
  for (let p = n.parentId ? byId.get(n.parentId) : undefined, guard = 0; p && guard < 64; p = p.parentId ? byId.get(p.parentId) : undefined, guard++) {
    x += p.position.x;
    y += p.position.y;
  }
  return { x, y };
}

/** The subsystem or story a node is directly in (frames don't count), or null: the top level. */
export function levelOf(id: string, nodes: AnyNode[] | ById): string | null {
  const byId = nodes instanceof Map ? nodes : byIdOf(nodes);
  for (let p = byId.get(id)?.parentId, guard = 0; p && guard < 64; p = byId.get(p)?.parentId, guard++) if (isLevel(byId.get(p))) return p;
  return null;
}

/** The levels from the top down to `level` (itself included). */
export function levelPath(level: string | null, nodes: AnyNode[]): GroupNode[] {
  const byId = byIdOf(nodes);
  const out: GroupNode[] = [];
  for (let l = level, guard = 0; l && guard < 64; l = levelOf(l, byId), guard++) {
    const n = byId.get(l);
    if (isLevel(n)) out.unshift(n);
  }
  return out;
}

/** Every block and group inside group `id`, at any depth. */
export function descendants(id: string, nodes: AnyNode[]): string[] {
  const kids = new Map<string, string[]>();
  for (const n of nodes) if (n.parentId) kids.set(n.parentId, [...(kids.get(n.parentId) ?? []), n.id]);
  const out: string[] = [];
  const walk = (p: string) => (kids.get(p) ?? []).forEach((c) => (out.push(c), walk(c)));
  walk(id);
  return out;
}

/**
 * What React Flow is shown at `level` (null: the top). Only that level's blocks and groups
 * show; a subsystem or story in it, or a collapsed frame, is one block whose ports are the
 * wires crossing its edge (`node::port`). Inside a level, a wire from outside comes in
 * through an In port at the left, and one going out leaves through an Out port at the
 * right. The editor's state keeps the real, flat wires.
 */
export function display<N extends AnyNode, E extends Edge>(
  nodes: N[],
  edges: E[],
  portType: (node: string, port: string, side: "inputs" | "outputs") => string,
  level: string | null = null,
): { nodes: N[]; edges: E[] } {
  const byId = byIdOf(nodes);
  const closed = (n: AnyNode | undefined) => !!n && isFrame(n) && (n.data.kind !== "visual" || n.data.collapsed);
  // Where `id` is at this level: "out" (not inside it), "here" (shown), or the closed group showing it.
  const place = (id: string): "out" | "here" | string => {
    if (id === level) return "out";
    let shownAs: string | null = null;
    for (let p = byId.get(id)?.parentId, guard = 0; guard < 64; p = byId.get(p!)?.parentId, guard++) {
      if (p === level || (!p && level === null)) return shownAs ?? "here";
      if (!p) return "out";
      if (closed(byId.get(p))) shownAs = p;
    }
    return "out";
  };
  const placeOf = new Map(nodes.map((n) => [n.id, place(n.id)]));
  const at = (id: string) => placeOf.get(id) ?? "out";

  const ports = new Map<string, { inputs: Map<string, PortDef>; outputs: Map<string, PortDef> }>();
  const portsOf = (g: string) => ports.get(g) ?? ports.set(g, { inputs: new Map(), outputs: new Map() }).get(g)!;
  const bounds = new Map<string, BoundaryData>();
  const shownEdges = edges.flatMap((e) => {
    const sp = at(e.source);
    const tp = at(e.target);
    if (sp === "out" && tp === "out") return [];
    if (e.source === level || e.target === level) return []; // a story's own step wires are outside it
    if (sp !== "here" && sp !== "out" && sp === tp) return []; // inside one closed group
    const sh = e.sourceHandle ?? "";
    const th = e.targetHandle ?? "";
    let shown = e;
    if (sp === "out") {
      const handle = portHandle(e.source, sh);
      bounds.set(`in:${handle}`, { dir: "in", label: `${e.source}.${sh}`, type: portType(e.source, sh, "outputs"), handle });
      shown = { ...shown, source: `in:${handle}`, sourceHandle: handle };
    } else if (sp !== "here") {
      const id = portHandle(e.source, sh);
      portsOf(sp).outputs.set(id, { id, type: portType(e.source, sh, "outputs") });
      shown = { ...shown, source: sp, sourceHandle: id };
    }
    if (tp === "out") {
      const handle = portHandle(e.target, th);
      bounds.set(`out:${handle}`, { dir: "out", label: `${e.target}.${th}`, type: portType(e.target, th, "inputs"), handle });
      shown = { ...shown, target: `out:${handle}`, targetHandle: handle };
    } else if (tp !== "here") {
      const id = portHandle(e.target, th);
      portsOf(tp).inputs.set(id, { id, type: portType(e.target, th, "inputs") });
      shown = { ...shown, target: tp, targetHandle: id };
    }
    return [shown];
  });

  const shownNodes = nodes.map((n) => {
    if (at(n.id) !== "here") return n.hidden ? n : { ...n, hidden: true };
    const plain = n.hidden ? { ...n, hidden: false } : n;
    if (!isFrame(n)) return plain;
    const d = n.data;
    if (d.kind === "visual" && !d.collapsed) return { ...plain, width: d.size.w, height: d.size.h, zIndex: -1 };
    const p = ports.get(n.id);
    return {
      ...plain,
      width: undefined,
      height: undefined,
      zIndex: d.kind === "visual" ? -1 : 0,
      data: { ...d, ports: { inputs: [...(p?.inputs.values() ?? [])], outputs: [...(p?.outputs.values() ?? [])] } },
    };
  });

  // In ports down the left of what the level shows, Out ports down its right.
  const boundary: AnyNode[] = [];
  if (bounds.size > 0) {
    const here = nodes.filter((n) => at(n.id) === "here");
    const xs = here.map((n) => absolute(n, byId).x);
    const ys = here.map((n) => absolute(n, byId).y);
    const right = here.map((n, i) => xs[i] + (n.measured?.width ?? BLOCK_W));
    const left = xs.length ? Math.min(...xs) : 0;
    const top = ys.length ? Math.min(...ys) : 0;
    const end = right.length ? Math.max(...right) : 300;
    let ins = 0;
    let outs = 0;
    for (const [id, data] of bounds) {
      const into = data.dir === "in";
      const row = into ? ins++ : outs++;
      const node: AnyNode = {
        id,
        type: "boundary",
        position: { x: into ? left - 90 - boundaryWidth(data) : end + 90, y: top + row * 52 },
        // Sized here: it is only shown, never in the editor's state, so React Flow can't
        // keep a measured size for it (and doesn't show a node it has no size for).
        width: boundaryWidth(data),
        height: 28,
        data,
        draggable: false,
        selectable: false,
        deletable: false,
        connectable: true,
      };
      // The same object while nothing about it changes: React Flow keeps a node's measured
      // handles only while it is given the same node.
      const key = JSON.stringify([node.position, data]);
      const had = boundaryCache.get(id);
      if (had?.key === key) boundary.push(had.node);
      else {
        boundaryCache.set(id, { key, node });
        boundary.push(node);
      }
    }
  }
  return { nodes: [...shownNodes, ...(boundary as N[])], edges: shownEdges };
}

const boundaryCache = new Map<string, { key: string; node: AnyNode }>();
const BLOCK_W = 190;
const BLOCK_H = 130;
/** An In or Out port's width: its tag and its name in small mono type. */
const boundaryWidth = (d: BoundaryData) => Math.round(48 + 7 * d.label.length);

/**
 * A group round `children`, which become its children. They all have the same parent (or
 * none) and their positions are in its space; the group goes in that parent too.
 */
export function frameAround<N extends AnyNode>(
  id: string,
  kind: GroupData["kind"],
  children: N[],
  options: { label?: string; collapsed?: boolean; seconds?: number; reset?: boolean; pos?: [number, number]; sides?: PortSides; inFrame?: boolean } = {},
): { frame: GroupNode; children: N[] } {
  const xs = children.map((n) => n.position.x);
  const ys = children.map((n) => n.position.y);
  const right = Math.max(...children.map((n) => n.position.x + (n.measured?.width ?? BLOCK_W)));
  const bottom = Math.max(...children.map((n) => n.position.y + (n.measured?.height ?? BLOCK_H)));
  const pad = 24;
  const x = options.pos?.[0] ?? Math.min(...xs) - pad;
  const y = options.pos?.[1] ?? Math.min(...ys) - pad - HEAD;
  const size = { w: Math.max(200, right - x + pad), h: Math.max(120, bottom - y + pad) };
  const parentId = children[0]?.parentId;
  const frame: GroupNode = {
    id,
    type: "frame",
    position: { x, y },
    deletable: false,
    zIndex: kind === "visual" ? -1 : 0,
    ...(parentId ? { parentId, ...(options.inFrame ? { extent: "parent" as const } : {}) } : {}),
    data: {
      kind,
      label: options.label ?? (kind === "story" ? "Story" : kind === "subsystem" ? "Subsystem" : "Frame"),
      collapsed: options.collapsed ?? false,
      seconds: options.seconds ?? (kind === "story" ? 1 : 0),
      reset: options.reset ?? false,
      size,
      ...(options.sides ? { sides: options.sides } : {}),
    },
  };
  return {
    frame,
    children: children.map((n) => ({
      ...n,
      parentId: id,
      extent: kind === "visual" ? ("parent" as const) : undefined,
      selected: false,
      position: { x: n.position.x - x, y: n.position.y - y },
    })),
  };
}

/** The group's contents back where the group was (one level up), where they are now; the group gone. */
export function withoutFrame<N extends AnyNode>(nodes: N[], id: string): N[] {
  const frame = nodes.find((n) => n.id === id);
  if (!frame) return nodes;
  const up = frame.parentId;
  const upIsFrame = !!up && nodes.some((n) => n.id === up && isFrame(n) && n.data.kind === "visual");
  return nodes
    .filter((n) => n.id !== id)
    .map((n) =>
      n.parentId === id
        ? {
            ...n,
            parentId: up,
            extent: upIsFrame ? ("parent" as const) : undefined,
            hidden: false,
            position: { x: frame.position.x + n.position.x, y: frame.position.y + n.position.y },
          }
        : n,
    );
}

/** Parents before their children, as React Flow wants; otherwise in the order given. */
export function ordered<N extends AnyNode>(nodes: N[]): N[] {
  const byId = byIdOf(nodes);
  const depth = (n: AnyNode) => {
    let d = 0;
    for (let p = n.parentId, guard = 0; p && guard < 64; p = byId.get(p)?.parentId, guard++) d++;
    return d;
  };
  return nodes.map((n, i) => ({ n, i, d: depth(n) })).sort((a, b) => a.d - b.d || a.i - b.i).map((e) => e.n);
}

/** The protocol's groups for these nodes. `pos` is where the group is on the whole canvas. */
export function toGroups(nodes: AnyNode[]): GraphGroup[] {
  const byId = byIdOf(nodes);
  return nodes.filter(isFrame).map((g) => {
    const at = absolute(g, byId);
    return {
      id: g.id,
      kind: g.data.kind,
      label: g.data.label,
      nodes: nodes.filter((n) => n.parentId === g.id && !isFrame(n)).map((n) => n.id),
      ...(g.parentId ? { parent: g.parentId } : {}),
      ...(g.data.kind === "visual" ? { collapsed: g.data.collapsed } : {}),
      pos: [Math.round(at.x), Math.round(at.y)],
      ...(g.data.kind === "story" ? { params: { seconds: g.data.seconds, reset: g.data.reset } } : {}),
      ...(g.data.sides ? { ports: g.data.sides } : {}),
    };
  });
}
