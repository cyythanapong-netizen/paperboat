import { Handle, NodeResizer, Position, type Edge, type Node, type NodeProps } from "@xyflow/react";
import { createContext, useContext } from "react";
import type { BlockDef, GraphGroup, PortDef } from "./protocol";

/**
 * Groups on the Flow page. Two kinds:
 * - **visual**: tidies the canvas. Its blocks run as if ungrouped (the sheet's flow type).
 * - **story**: one step of a Step flow (ports in → next). When a path arrives, its blocks
 *   run once per clock step for `seconds` (0: once); then the path moves on, and what the
 *   story computed stays on its outputs for the next story to read.
 * Collapsed, a group shows as one block whose ports are the wires crossing its edge.
 */
export type GroupData = {
  kind: "visual" | "story";
  label: string;
  collapsed: boolean;
  seconds: number;
  /** Its size when open. */
  size: { w: number; h: number };
  /** Filled for display: the ports a collapsed group shows (`node::port` ids). */
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

/** A collapsed group's handle for an inner port: `node::port`. */
export const SEP = "::";
export const portHandle = (node: string, port: string) => `${node}${SEP}${port}`;
export const splitHandle = (handle: string): [string, string] | null => {
  const i = handle.indexOf(SEP);
  return i < 0 ? null : [handle.slice(0, i), handle.slice(i + SEP.length)];
};

export const isFrame = (n: Node): n is GroupNode => n.type === "frame";

/** Actions a group's header offers, provided by the Flow editor. */
export const GroupActions = createContext<{
  update: (id: string, change: Partial<GroupData>) => void;
  ungroup: (id: string) => void;
  active: ReadonlySet<string>;
}>({ update: () => {}, ungroup: () => {}, active: new Set() });

const HEAD = 34;

export function GroupBox({ id, data, selected }: NodeProps<GroupNode>) {
  const { update, ungroup, active } = useContext(GroupActions);
  const story = data.kind === "story";
  const ports = data.ports ?? { inputs: [], outputs: [] };
  return (
    <div className={`frame frame-${data.kind}${data.collapsed ? " collapsed" : ""}${active.has(id) ? " active" : ""}`}>
      {!data.collapsed && (
        <NodeResizer
          isVisible={selected}
          minWidth={180}
          minHeight={100}
          onResizeEnd={(_, p) => update(id, { size: { w: p.width, h: p.height } })}
        />
      )}
      <div className="frame-head" style={{ height: HEAD }}>
        {story && <Handle type="target" position={Position.Left} id="in" className="step-handle" />}
        <button className="frame-toggle nodrag" title={data.collapsed ? "Open" : "Collapse"} onClick={() => update(id, { collapsed: !data.collapsed })}>
          {data.collapsed ? "▸" : "▾"}
        </button>
        <span className={`frame-kind kind-${data.kind}`}>{story ? "story" : "group"}</span>
        <input className="frame-label nodrag" value={data.label} onChange={(e) => update(id, { label: e.target.value })} />
        {story && (
          <label className="frame-seconds" title="How long the story runs when a path arrives (0: once)">
            <input
              className="nodrag"
              type="number"
              min={0}
              step="any"
              value={data.seconds}
              onChange={(e) => update(id, { seconds: Math.max(0, e.target.valueAsNumber || 0) })}
            />
            s
          </label>
        )}
        <button className="remove nodrag" title="Ungroup (keeps the blocks)" onClick={() => ungroup(id)}>
          ⊟
        </button>
        {story && <Handle type="source" position={Position.Right} id="next" className="step-handle" />}
      </div>
      {data.collapsed && (
        <div className="frame-ports">
          {ports.inputs.map((p) => (
            <div key={p.id} className={`port in port-${p.type}`}>
              <Handle type="target" position={Position.Left} id={p.id} />
              {p.id.replace(SEP, ".")}
            </div>
          ))}
          {ports.outputs.map((p) => (
            <div key={p.id} className={`port out port-${p.type}`}>
              {p.id.replace(SEP, ".")}
              <Handle type="source" position={Position.Right} id={p.id} />
            </div>
          ))}
          {ports.inputs.length + ports.outputs.length === 0 && <div className="muted frame-empty">no wires cross it</div>}
        </div>
      )}
    </div>
  );
}

type AnyNode = Node & { data: unknown };

/** Where a node is on the canvas, whether or not it's inside a group. */
export function absolute(n: Node, nodes: Node[]): { x: number; y: number } {
  const parent = n.parentId ? nodes.find((p) => p.id === n.parentId) : undefined;
  return parent ? { x: parent.position.x + n.position.x, y: parent.position.y + n.position.y } : n.position;
}

/**
 * What React Flow is shown: a collapsed group's blocks hidden, its wires drawn to its own
 * ports (`node::port`), wires inside it hidden. The editor's state keeps the real wires.
 */
export function display<N extends AnyNode, E extends Edge>(
  nodes: N[],
  edges: E[],
  portType: (node: string, port: string, side: "inputs" | "outputs") => string,
): { nodes: N[]; edges: E[] } {
  const collapsed = new Set(nodes.filter((n) => isFrame(n) && (n.data as GroupData).collapsed).map((n) => n.id));
  const parent = new Map(nodes.map((n) => [n.id, n.parentId]));
  const closedIn = (id: string) => {
    const p = parent.get(id);
    return p && collapsed.has(p) ? p : undefined;
  };
  const ports = new Map<string, { inputs: Map<string, PortDef>; outputs: Map<string, PortDef> }>();
  const portsOf = (g: string) => ports.get(g) ?? ports.set(g, { inputs: new Map(), outputs: new Map() }).get(g)!;
  const shownEdges = edges.flatMap((e) => {
    const sg = closedIn(e.source);
    const tg = closedIn(e.target);
    if (sg && sg === tg) return [];
    let shown = e;
    if (sg) {
      const id = portHandle(e.source, e.sourceHandle ?? "");
      portsOf(sg).outputs.set(id, { id, type: portType(e.source, e.sourceHandle ?? "", "outputs") });
      shown = { ...shown, source: sg, sourceHandle: id };
    }
    if (tg) {
      const id = portHandle(e.target, e.targetHandle ?? "");
      portsOf(tg).inputs.set(id, { id, type: portType(e.target, e.targetHandle ?? "", "inputs") });
      shown = { ...shown, target: tg, targetHandle: id };
    }
    return [shown];
  });
  const shownNodes = nodes.map((n) => {
    if (isFrame(n)) {
      const d = n.data as GroupData;
      if (!d.collapsed) return { ...n, width: d.size.w, height: d.size.h };
      const p = ports.get(n.id);
      return {
        ...n,
        width: undefined,
        height: undefined,
        data: { ...d, ports: { inputs: [...(p?.inputs.values() ?? [])], outputs: [...(p?.outputs.values() ?? [])] } },
      };
    }
    return closedIn(n.id) ? { ...n, hidden: true } : n;
  });
  return { nodes: shownNodes as N[], edges: shownEdges };
}

const BLOCK_W = 190;
const BLOCK_H = 130;

/** A frame around `children` (absolute positions), which become its children. */
export function frameAround<N extends AnyNode>(
  id: string,
  kind: GroupData["kind"],
  children: N[],
  options: { label?: string; collapsed?: boolean; seconds?: number; pos?: [number, number] } = {},
): { frame: GroupNode; children: N[] } {
  const xs = children.map((n) => n.position.x);
  const ys = children.map((n) => n.position.y);
  const right = Math.max(...children.map((n) => n.position.x + (n.measured?.width ?? BLOCK_W)));
  const bottom = Math.max(...children.map((n) => n.position.y + (n.measured?.height ?? BLOCK_H)));
  const pad = 24;
  const x = options.pos?.[0] ?? Math.min(...xs) - pad;
  const y = options.pos?.[1] ?? Math.min(...ys) - pad - HEAD;
  const size = { w: Math.max(200, right - x + pad), h: Math.max(120, bottom - y + pad) };
  const frame: GroupNode = {
    id,
    type: "frame",
    position: { x, y },
    deletable: false,
    zIndex: -1,
    data: {
      kind,
      label: options.label ?? (kind === "story" ? "Story" : "Group"),
      collapsed: options.collapsed ?? false,
      seconds: options.seconds ?? (kind === "story" ? 1 : 0),
      size,
    },
  };
  return {
    frame,
    children: children.map((n) => ({
      ...n,
      parentId: id,
      extent: "parent" as const,
      selected: false,
      position: { x: n.position.x - x, y: n.position.y - y },
    })),
  };
}

/** The group's blocks back on the canvas, where they are now; the frame gone. */
export function withoutFrame<N extends AnyNode>(nodes: N[], id: string): N[] {
  const frame = nodes.find((n) => n.id === id);
  if (!frame) return nodes;
  return nodes
    .filter((n) => n.id !== id)
    .map((n) =>
      n.parentId === id
        ? { ...n, parentId: undefined, extent: undefined, hidden: false, position: absolute(n, nodes) }
        : n,
    );
}

/** The protocol's groups for these nodes. */
export function toGroups(nodes: AnyNode[]): GraphGroup[] {
  return nodes.filter(isFrame).map((g) => ({
    id: g.id,
    kind: g.data.kind,
    label: g.data.label,
    nodes: nodes.filter((n) => n.parentId === g.id).map((n) => n.id),
    collapsed: g.data.collapsed,
    pos: [Math.round(g.position.x), Math.round(g.position.y)],
    ...(g.data.kind === "story" ? { params: { seconds: g.data.seconds } } : {}),
  }));
}
