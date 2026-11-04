import {
  addEdge,
  Background,
  BaseEdge,
  EdgeLabelRenderer,
  getBezierPath,
  getSmoothStepPath,
  MarkerType,
  MiniMap,
  Position,
  ReactFlow,
  useInternalNode,
  useReactFlow,
  useStoreApi,
  type Connection,
  type Edge,
  type EdgeProps,
  type IsValidConnection,
  type Node,
  type NodeProps,
  type OnEdgesChange,
  type OnNodesChange,
} from "@xyflow/react";
import { createContext, useContext, useEffect, useMemo, useRef, useState, type Dispatch, type DragEvent, type SetStateAction } from "react";
import { DRAG_TYPE, dragging, QuickAdd, type Item } from "./Browser";
import { NumberField } from "./NumberField";
import { RemoveIcon } from "./RemoveIcon";
import { connectionOf, nearestAccepting, PortHandle, SNAP, WireDragContext, type Anchor } from "./wireSnap";
import {
  display,
  frameAround,
  GroupActions,
  GroupBox,
  isFrame,
  splitHandle,
  STORY_DEF,
  toGroups,
  withoutFrame,
  type GroupData,
  type GroupNode,
} from "./groups";
import type { BlockDef, Graph, ObjectDef, ParamDef, ParamValue, PortDef, Station } from "./protocol";

/**
 * `missing`: the backend's catalog has no block of this type (an old graph, another
 * backend's, a block renamed in code). The node keeps its type, params and wires, drawn
 * red from what it had, until the type comes back or the user replaces it.
 */
export type BlockData = { def: BlockDef; params: Record<string, ParamValue>; missing?: boolean };
export type BlockNode = Node<BlockData, "block">;
/** What the Flow canvas holds: blocks, and the groups (frames) around some of them. */
export type FlowNode = BlockNode | GroupNode;
export const isBlock = (n: FlowNode): n is BlockNode => n.type === "block";
/** `type`: the type of the ports it joins: `step` (control passes along it), `signal`, `image`… */
export type Wire = Edge<{ type: string }, "wire">;

/** What the last run said about blocks (`node.state`): where a Step flow is, which failed. */
export const RunStates = createContext<{
  active: ReadonlySet<string>;
  failed: ReadonlySet<string>;
  /** How far a block's long step has got (node.progress). */
  progress: Readonly<Record<string, number>>;
}>({
  active: new Set(),
  failed: new Set(),
  progress: {},
});

/** A magic block's definition in the form its `form` param picks (its ports are that form's). */
export function inForm(def: BlockDef, params: Record<string, ParamValue>): BlockDef {
  const form = def.forms?.find((f) => f.name === params.form);
  return form ? { ...def, inputs: form.inputs, outputs: form.outputs } : def;
}

/** A block's ports as they are now (a magic block's: its form's). */
export const portsOf = (n: BlockNode) => inForm(n.data.def, n.data.params);

const portType = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  def?.[side].find((p) => p.id === port)?.type;

/** Generic node: everything it shows comes from the backend's BlockDef. */
function Block({ id, data, selected }: NodeProps<BlockNode>) {
  const { updateNodeData, deleteElements } = useReactFlow<BlockNode, Wire>();
  const states = useContext(RunStates);
  const active = states.active.has(id);
  const progress = states.progress[id];
  const { params, missing } = data;
  const def = inForm(data.def, params);
  const failed = states.failed.has(id);
  const step = hasStepPorts(def);
  const classes = ["block", `kind-${def.kind}`, step && "step", selected && "selected", active && "active", def.linked && "linked", missing && "missing", failed && "failed"];
  // The head's tag says what is happening to the block, else what it is: words, not colours.
  const tag = missing
    ? "missing"
    : failed
      ? "failed"
      : active
        ? "running"
        : def.linked
          ? "station"
          : step
            ? "step"
            : def.kind === "function"
              ? "fn"
              : def.kind;
  return (
    <div
      className={classes.filter(Boolean).join(" ")}
      title={missing ? `This backend has no "${def.type}" block: its params and wires are kept. Replace or delete it.` : undefined}
    >
      <div className="block-title">
        <span className="block-name" title={def.linked ? "Linked to a station object: set its properties, or remove it, on the Station" : undefined}>
          {missing ? def.type : def.label}
          {def.linked && <span className="linked-tag"> {id}</span>}
        </span>
        <span className={`block-tag tag-${tag}`}>{tag}</span>
        {selected && !def.linked && (
          <button className="remove nodrag" title="Delete block" onClick={() => deleteElements({ nodes: [{ id }] })}>
            <RemoveIcon />
          </button>
        )}
      </div>
      {active && progress !== undefined && progress < 1 && (
        <div className="block-progress" title={`${Math.round(progress * 100)} %`}>
          <div style={{ width: `${progress * 100}%` }} />
        </div>
      )}
      {def.inputs.map((p) => (
        <div key={p.id} className={`port in port-${p.type}`}>
          <PortHandle type="target" position={Position.Left} id={p.id} />
          {p.id}
        </div>
      ))}
      {def.outputs.map((p) => (
        <div key={p.id} className={`port out port-${p.type}`}>
          {p.id}
          <PortHandle type="source" position={Position.Right} id={p.id} />
        </div>
      ))}
      {def.params.map((p) => (
        <label key={p.id} className="param">
          {p.id}
          <ParamInput
            def={p}
            value={params[p.id]}
            disabled={missing}
            set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })}
          />
        </label>
      ))}
    </div>
  );
}

/** A number field, or for a `key` param a field that takes the next key you press. */
export function ParamInput({
  def,
  value,
  set,
  disabled,
}: {
  def: ParamDef;
  value: ParamValue;
  set: (v: ParamValue) => void;
  disabled?: boolean;
}) {
  if (def.type === "boolean") {
    return (
      <input
        className="nodrag bool-param"
        type="checkbox"
        disabled={disabled}
        checked={value === true || value === 1}
        onChange={(e) => set(e.target.checked)}
      />
    );
  }
  if (def.type === "enum") {
    return (
      <select className="nodrag enum-param" disabled={disabled} value={String(value)} onChange={(e) => set(e.target.value)}>
        {def.options.map((o) => (
          <option key={o}>{o}</option>
        ))}
      </select>
    );
  }
  if (def.type === "file" || def.type === "text") {
    return (
      <input
        className="nodrag text-param"
        disabled={disabled}
        value={String(value)}
        title={def.type === "file" ? "A file, relative to the backend's descriptor" : undefined}
        onChange={(e) => set(e.target.value)}
      />
    );
  }
  if (def.type === "key") {
    return (
      <input
        className="nodrag key-param"
        disabled={disabled}
        readOnly
        value={String(value)}
        title="Click, then press a key (Esc to keep)"
        onKeyDown={(e) => {
          e.preventDefault();
          if (e.code !== "Escape") set(e.code);
          e.currentTarget.blur();
        }}
      />
    );
  }
  return (
    <NumberField className="nodrag num" disabled={disabled} value={Number(value)} onChange={set} />
  );
}

/** The bottom edge of a node on the canvas (inside a group or not). */
function useBottom(id: string): number | undefined {
  const node = useInternalNode(id);
  if (!node) return undefined;
  return node.internals.positionAbsolute.y + (node.measured.height ?? 0);
}

/**
 * A wire that runs back (its target left of its source, as in a loop: Rotate → Repeat)
 * goes around the blocks instead of under them: out to the right, down below both blocks,
 * back left, and into the target from the left, with rounded corners.
 */
function loopPath(sx: number, sy: number, tx: number, ty: number, below: number): [string, number, number] {
  const out = 24;  // how far it leaves a port before turning
  const r = 10;    // corner radius
  const pts: [number, number][] = [
    [sx, sy],
    [sx + out, sy],
    [sx + out, below],
    [tx - out, below],
    [tx - out, ty],
    [tx, ty],
  ];
  let d = `M ${pts[0][0]},${pts[0][1]}`;
  for (let i = 1; i < pts.length - 1; i++) {
    const [px, py] = pts[i - 1];
    const [cx, cy] = pts[i];
    const [nx, ny] = pts[i + 1];
    const ra = Math.min(r, Math.hypot(cx - px, cy - py) / 2, Math.hypot(nx - cx, ny - cy) / 2);
    const a = [cx - Math.sign(cx - px) * ra, cy - Math.sign(cy - py) * ra];
    const b = [cx + Math.sign(nx - cx) * ra, cy + Math.sign(ny - cy) * ra];
    d += ` L ${a[0]},${a[1]} Q ${cx},${cy} ${b[0]},${b[1]}`;
  }
  d += ` L ${tx},${ty}`;
  return [d, (sx + tx) / 2, below];
}

/** A wire: a curve for signals, a right-angled arrow for steps, around the blocks when it runs back, and × when selected. */
/** The wire a dragged card would be inserted into, if any (drawn thicker). */
const HotWire = createContext<string | null>(null);

function WireEdge({ id, source, target, data, selected, markerEnd, ...geometry }: EdgeProps<Wire>) {
  const { deleteElements } = useReactFlow();
  const hot = useContext(HotWire) === id;
  const step = data?.type === "step";
  const sourceBottom = useBottom(source);
  const targetBottom = useBottom(target);
  const back = geometry.targetX < geometry.sourceX + 8;
  const [path, x, y] = back
    ? loopPath(
        geometry.sourceX,
        geometry.sourceY,
        geometry.targetX,
        geometry.targetY,
        Math.max(sourceBottom ?? geometry.sourceY, targetBottom ?? geometry.targetY, geometry.sourceY, geometry.targetY) + 28,
      )
    : (step ? getSmoothStepPath : getBezierPath)(geometry);
  return (
    <>
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={`wire-${data?.type ?? "signal"}${hot ? " drop-target" : ""}`} />
      {/* What a dropped card finds under the pointer: a wide, invisible stroke tagged with the wire. */}
      <path d={path} className="wire-hit" data-wire={id} />
      {selected && (
        <EdgeLabelRenderer>
          <button
            className="remove nodrag nopan"
            title="Delete wire"
            style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y}px)` }}
            onClick={() => deleteElements({ edges: [{ id }] })}
          >
            <RemoveIcon />
          </button>
        </EdgeLabelRenderer>
      )}
    </>
  );
}

const nodeTypes = { block: Block, frame: GroupBox };
const edgeTypes = { wire: WireEdge };

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

function makeWire(c: Connection, type: string): Wire {
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
function connect(wires: Wire[], c: Connection, type: string): Wire[] {
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
function missingDef(
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

/** Wire graph → editor state. A node whose type isn't in the catalog stays, as a missing block. */
export function fromGraph(graph: Graph, blocks: BlockDef[]): { nodes: FlowNode[]; edges: Wire[] } {
  const defs = new Map(blocks.map((b) => [b.type, b]));
  const wires = graph.edges.map(({ from: [s, sp], to: [t, tp] }) => ({ source: s, sourceHandle: sp, target: t, targetHandle: tp }));
  const nodes = graph.nodes.map((n, i) => {
    const [x, y] = n.pos ?? [0, 120 * i];
    const def = defs.get(n.type);
    if (def) return makeNode(def, n.id, x, y, n.params);
    const node = makeNode(missingDef(n.type, n.params ?? {}, wires, n.id), n.id, x, y, n.params);
    return { ...node, data: { ...node.data, missing: true } };
  });
  const byId = new Map<string, BlockDef>(nodes.map((n) => [n.id, inForm(n.data.def, n.data.params)]));
  // Groups: frames around their blocks (parents before children, as React Flow wants).
  let all: FlowNode[] = nodes;
  const frames: FlowNode[] = [];
  for (const g of graph.groups ?? []) {
    const inside = all.filter((n): n is BlockNode => isBlock(n) && g.nodes.includes(n.id) && !n.parentId);
    if (inside.length === 0) continue;
    const { frame, children } = frameAround(g.id, g.kind, inside, {
      label: g.label,
      collapsed: g.collapsed,
      seconds: g.params?.seconds,
      reset: g.params?.reset,
      pos: g.pos,
    });
    if (g.kind === "story") byId.set(g.id, STORY_DEF);
    frames.push(frame);
    const moved = new Map(children.map((c) => [c.id, c]));
    all = all.map((n) => moved.get(n.id) ?? n);
  }
  const edges = wires.map((w) => makeWire(w, portType(byId.get(w.source), w.sourceHandle, "outputs") ?? "signal"));
  return { nodes: [...frames, ...all], edges };
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
      return { ...n, deletable: !def.linked, data: { def, params: { ...defaults, ...kept } } };
    }
    const wires = edges.map((e) => ({ ...e, type: e.data?.type }));
    return { ...n, deletable: true, data: { ...n.data, def: missingDef(n.data.def.type, n.data.params, wires, n.id, n.data.def), missing: true } };
  });
  return changed ? out : nodes;
}

export function toGraph(nodes: FlowNode[], edges: Wire[]): Graph {
  const groups = toGroups(nodes);
  return {
    nodes: nodes.filter(isBlock).map((n) => ({ id: n.id, type: n.data.def.type, params: n.data.params })),
    edges: edges.map((e) => ({ from: [e.source, e.sourceHandle ?? ""], to: [e.target, e.targetHandle ?? ""] })),
    ...(groups.length > 0 ? { groups } : {}),
  };
}

/** A new block at `at` (flow coordinates, its top-left), selected and the only one selected. */
export function withBlock(nodes: FlowNode[], def: BlockDef, at: { x: number; y: number }): [FlowNode[], string] {
  const id = freeId(def.type, nodes);
  const node = { ...makeNode(def, id, Math.round(at.x), Math.round(at.y)), selected: true };
  return [[...nodes.map((n) => (n.selected ? { ...n, selected: false } : n)), node], id];
}

const typeOf = (p: PortDef) => p.type ?? "signal";
const BLOCK_SIZE = { w: 156, h: 70 };

/** What dropping a card here would do. */
type Plan =
  | { kind: "place" }
  | { kind: "insert"; wire: Wire; input: PortDef; output: PortDef }
  | { kind: "port"; node: string; handle: string; side: "source" | "target"; port: PortDef; hx: number; hy: number };

type Props = {
  blocks: BlockDef[];
  nodes: FlowNode[];
  edges: Wire[];
  setNodes: Dispatch<SetStateAction<FlowNode[]>>;
  setEdges: Dispatch<SetStateAction<Wire[]>>;
  onNodesChange: OnNodesChange<FlowNode>;
  onEdgesChange: OnEdgesChange<Wire>;
  /** Show the overview map: when the Flow page is the one in view, not among all pages (Commander). */
  minimap: boolean;
};

/** Blocks by palette group, groups in catalog order. */
export function FlowEditor({ blocks, nodes, edges, setNodes, setEdges, onNodesChange, onEdgesChange, minimap }: Props) {
  const states = useContext(RunStates);
  const defOf = (id: string): BlockDef | undefined => {
    const n = nodes.find((x) => x.id === id);
    if (!n) return undefined;
    if (isBlock(n)) return portsOf(n);
    return n.data.kind === "story" ? STORY_DEF : undefined;
  };
  // A wire drawn to a collapsed group's port is a wire to the block inside it.
  const real = (c: Connection): Connection => {
    const s = c.sourceHandle ? splitHandle(c.sourceHandle) : null;
    const t = c.targetHandle ? splitHandle(c.targetHandle) : null;
    return {
      source: s ? s[0] : c.source,
      sourceHandle: s ? s[1] : c.sourceHandle,
      target: t ? t[0] : c.target,
      targetHandle: t ? t[1] : c.targetHandle,
    };
  };
  const wireType = (c: Connection | Wire) => portType(defOf(c.source), c.sourceHandle, "outputs") ?? "signal";

  // A wire joins ports of one type: signal to signal, step to step, image to image…
  const isValidConnection: IsValidConnection<Wire> = (shown) => {
    const c = real(shown as Connection);
    return c.source !== c.target && wireType(c) === portType(defOf(c.target), c.targetHandle, "inputs");
  };

  const onConnect = (shown: Connection) => {
    const c = real(shown);
    setEdges((ws) => connect(ws, c, wireType(c)));
  };

  // Grouping: a frame around the selected blocks (not already in a group). A story holds
  // signal blocks only: it is itself the step.
  const selected = nodes.filter((n): n is BlockNode => isBlock(n) && !!n.selected && !n.parentId);
  const selectedFrame = nodes.find((n) => isFrame(n) && n.selected);
  const canStory = selected.length > 0 && selected.every((n) => !hasStepPorts(portsOf(n)));
  const group = (kind: GroupData["kind"]) =>
    setNodes((ns) => {
      const picked = ns.filter((n): n is BlockNode => isBlock(n) && !!n.selected && !n.parentId);
      if (picked.length === 0) return ns;
      const { frame, children } = frameAround(freeId(kind === "story" ? "story" : "group", ns), kind, picked);
      const moved = new Map(children.map((c) => [c.id, c]));
      return [frame, ...ns.filter((n) => !moved.has(n.id)), ...children];
    });
  const actions = useMemo(
    () => ({
      update: (id: string, change: Partial<GroupData>) =>
        setNodes((ns) => ns.map((n) => (n.id === id && isFrame(n) ? { ...n, data: { ...n.data, ...change } } : n))),
      ungroup: (id: string) => {
        setNodes((ns) => withoutFrame(ns, id));
        setEdges((ws) => ws.filter((w) => w.source !== id && w.target !== id)); // a story's step wires
      },
    }),
    [setNodes, setEdges],
  );
  const runStates = useContext(RunStates);
  const shown = display(nodes, edges, (node, port, side) => portType(defOf(node), port, side) ?? "signal");

  // Dragging a wire's end onto another port moves it; dropping it anywhere else deletes it.
  const moved = useRef(true);
  const onReconnect = (old: Wire, c: Connection) => {
    moved.current = true;
    setEdges((ws) => {
      const rest = ws.filter((w) => w.id !== old.id);
      return connect(rest, c, wireType(c));
    });
  };
  const onReconnectEnd = (_: unknown, wire: Wire, __: unknown, state: { isValid: boolean | null }) => {
    // The drop missed every port that accepts it, but one does within reach: join that one.
    if (!state.isValid && anchor && targetNow.current) onReconnect(wire, connectionOf(anchor, targetNow.current.nodeId, targetNow.current.handleId));
    if (!moved.current) setEdges((ws) => ws.filter((w) => w.id !== wire.id));
    moved.current = true;
    endWire();
  };

  // ---- Adding: quick add at the cursor, and cards dropped from the browser (#50) ----
  const { screenToFlowPosition } = useReactFlow();

  // ---- Dragging a wire (#52): the port it would join is raised, ports that can't take it dim ----
  const store = useStoreApi();
  const [anchor, setAnchor] = useState<Anchor | null>(null);
  const [target, setTarget] = useState<{ nodeId: string; handleId: string | null } | null>(null);
  const targetNow = useRef(target); // what a drop would join, read when the pointer is released
  useEffect(() => {
    if (!anchor) return;
    const move = (e: PointerEvent) => {
      const next = nearestAccepting(store.getState(), screenToFlowPosition({ x: e.clientX, y: e.clientY }), SNAP.connect, anchor, isValidConnection);
      if (next?.nodeId === targetNow.current?.nodeId && next?.handleId === targetNow.current?.handleId) return;
      targetNow.current = next;
      setTarget(next); // re-render only when the target changes, not on every move
    };
    window.addEventListener("pointermove", move);
    return () => window.removeEventListener("pointermove", move);
  }); // eslint-disable-line react-hooks/exhaustive-deps -- isValidConnection follows the nodes: re-bind each render
  const startWire = (a: Anchor) => {
    targetNow.current = null;
    setTarget(null);
    setAnchor(a);
  };
  const endWire = () => {
    targetNow.current = null;
    setTarget(null);
    setAnchor(null);
  };
  const wireDrag = anchor
    ? {
        target,
        accepts: (nodeId: string, handleId: string | null, side: "source" | "target") =>
          side !== anchor.type && isValidConnection(connectionOf(anchor, nodeId, handleId)),
      }
    : null;
  const wrap = useRef<HTMLDivElement>(null);
  const addable = useMemo<Item[]>(() => blocks.filter((b) => !b.linked).map((def) => ({ kind: "block", def })), [blocks]);
  const [quick, setQuick] = useState<{ x: number; y: number } | null>(null);
  const pointer = useRef<{ x: number; y: number } | null>(null); // over the canvas, else null

  const place = (def: BlockDef, screen: { x: number; y: number }) => {
    const at = screenToFlowPosition(screen);
    let id = "";
    setNodes((ns) => {
      const [next, made] = withBlock(ns, def, { x: at.x - BLOCK_SIZE.w / 2, y: at.y - 16 });
      id = made;
      return next;
    });
    return () => id;
  };

  // `/` with the pointer over the canvas opens quick add there (it is then not a key for
  // the backend); so does a double-click on empty canvas.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (e.key !== "/" || !pointer.current || e.ctrlKey || e.metaKey || e.altKey) return;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable)) return;
      e.preventDefault();
      setQuick({ ...pointer.current });
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, []);

  // A dragged card: plan what dropping it here would do, show that, and do it on drop.
  const [hint, setHint] = useState<{ plan: Plan; x: number; y: number; label: string } | null>(null);
  const plan = (def: BlockDef, x: number, y: number): Plan => {
    // On a wire whose type the block both takes and gives: insert it.
    const hit = document.elementsFromPoint(x, y).find((el) => el instanceof SVGElement && el.dataset.wire);
    const wire = hit ? edges.find((w) => w.id === (hit as SVGElement).dataset.wire) : undefined;
    if (wire) {
      const t = wire.data?.type ?? "signal";
      const input = def.inputs.find((q) => typeOf(q) === t);
      const output = def.outputs.find((q) => typeOf(q) === t);
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
      const t = portType(defOf(node), handle, side === "source" ? "outputs" : "inputs") ?? "signal";
      const port = (side === "source" ? def.inputs : def.outputs).find((q) => typeOf(q) === t);
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
    const node = { ...makeNode(def, id, Math.round(at.x - BLOCK_SIZE.w / 2), Math.round(at.y - 16)), selected: true };
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
  const box = wrap.current?.getBoundingClientRect();
  const rel = (x: number, y: number) => ({ x: x - (box?.left ?? 0), y: y - (box?.top ?? 0) });

  return (
    <GroupActions.Provider value={{ ...actions, active: runStates.active }}>
    <HotWire.Provider value={hint?.plan.kind === "insert" ? hint.plan.wire.id : null}>
    <div className="flow">
      <div className="flow-tools">
        <span className="lbl">Selection</span>
        <button disabled={selected.length === 0} title="Frame the selected blocks (tidy; they run as before)" onClick={() => group("visual")}>
          ▭ Group
        </button>
        <button
          disabled={!canStory}
          title="Make the selected signal blocks a story: one step of a Step flow that runs them for its seconds"
          onClick={() => group("story")}
        >
          ▶ Story
        </button>
        <button disabled={!selectedFrame} onClick={() => selectedFrame && actions.ungroup(selectedFrame.id)}>
          ⊟ Ungroup
        </button>
        <span className="lbl flow-tip">
          <span className="kbd">/</span> or double-click the canvas to add a block
        </span>
      </div>
      <div
        className="flow-canvas"
        ref={wrap}
        onMouseMove={(e) => (pointer.current = { x: e.clientX, y: e.clientY })}
        onMouseLeave={() => (pointer.current = null)}
        onDoubleClick={(e) => {
          // Empty canvas only: not a block, a wire, a port or a control.
          const t = e.target as Element;
          if (!t.closest(".react-flow__node, .react-flow__edge, .react-flow__handle, .react-flow__panel, button, input, select"))
            setQuick({ x: e.clientX, y: e.clientY });
        }}
        onDragOver={onDragOver}
        onDragLeave={(e) => !wrap.current?.contains(e.relatedTarget as globalThis.Node | null) && setHint(null)}
        onDrop={onDrop}
      >
        <WireDragContext.Provider value={wireDrag}>
        <ReactFlow
          nodes={shown.nodes}
          edges={shown.edges}
          nodeTypes={nodeTypes}
          edgeTypes={edgeTypes}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          onConnect={onConnect}
          isValidConnection={isValidConnection}
          onReconnect={onReconnect}
          onReconnectStart={(_, wire, end) => {
            moved.current = false;
            // The end that stays put is the anchor.
            startWire(end === "target" ? { nodeId: wire.source, handleId: wire.sourceHandle ?? null, type: "source" } : { nodeId: wire.target, handleId: wire.targetHandle ?? null, type: "target" });
          }}
          onReconnectEnd={onReconnectEnd}
          onConnectStart={(_, from) => from.nodeId && from.handleType && startWire({ nodeId: from.nodeId, handleId: from.handleId, type: from.handleType })}
          onConnectEnd={(_, state) => {
            // Same: nothing accepting was snapped to, but one is within reach.
            if (!state.isValid && anchor && targetNow.current) onConnect(connectionOf(anchor, targetNow.current.nodeId, targetNow.current.handleId));
            endWire();
          }}
          connectionRadius={SNAP.connect}
          reconnectRadius={SNAP.reconnect}
          deleteKeyCode={["Backspace", "Delete"]}
          multiSelectionKeyCode={["Control", "Meta"]}
          panActivationKeyCode={null} // Space belongs to the backend (Key blocks)
          zoomOnDoubleClick={false} // double-click is quick add
          fitView
        >
          <Background />
          {minimap && (
            <MiniMap
              pannable
              zoomable
              ariaLabel="Overview of the flow: click or drag to move the view"
              // Words, not colours, tell kinds apart (DESIGN.md): grey blocks, groups as outlines,
              // the signal colour for running, red for failed.
              nodeColor={(n) => (n.type === "frame" ? "transparent" : states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeColor={(n) => (states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeWidth={3}
              nodeBorderRadius={2}
              maskColor="var(--surface-float)"
              maskStrokeColor="var(--line-ink)"
              maskStrokeWidth={1}
            />
          )}
        </ReactFlow>
        </WireDragContext.Provider>
        {hint && (
          <div className="drop-hint" aria-live="polite">
            {hint.plan.kind === "place" && (
              <div className="drop-outline" style={{ left: rel(hint.x, hint.y).x - BLOCK_SIZE.w / 2, top: rel(hint.x, hint.y).y - 16 }} />
            )}
            {hint.plan.kind === "port" && (
              <svg className="drop-wire">
                <line x1={rel(hint.plan.hx, hint.plan.hy).x} y1={rel(hint.plan.hx, hint.plan.hy).y} x2={rel(hint.x, hint.y).x} y2={rel(hint.x, hint.y).y} />
                <circle cx={rel(hint.plan.hx, hint.plan.hy).x} cy={rel(hint.plan.hx, hint.plan.hy).y} r={5} />
              </svg>
            )}
            <span className="drop-label" style={{ left: rel(hint.x, hint.y).x + 14, top: rel(hint.x, hint.y).y + 18 }}>
              {hint.label}
            </span>
          </div>
        )}
      </div>
      {quick && (
        <QuickAdd
          items={addable}
          at={quick}
          onClose={() => setQuick(null)}
          onPick={(it) => {
            if (it.kind === "block") place(it.def, quick);
            setQuick(null);
          }}
        />
      )}
      <p className="hint muted">
        Click a block or wire, then Delete or ×. Shift-drag to select several; Ctrl-click to add one. Drag a wire's end
        off its port to remove it. Group or Story the selected blocks; ▾ collapses a group. Ctrl+Z undoes, Ctrl+Shift+Z redoes.
      </p>
    </div>
    </HotWire.Provider>
    </GroupActions.Provider>
  );
}

const hasStepPorts = (def: BlockDef) => [...def.inputs, ...def.outputs].some((p) => p.type === "step");
