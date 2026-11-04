import {
  addEdge,
  Background,
  BaseEdge,
  EdgeLabelRenderer,
  getBezierPath,
  getSmoothStepPath,
  MarkerType,
  MiniMap,
  NodeToolbar,
  Panel,
  useStore,
  Position,
  ReactFlow,
  SelectionMode,
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
  type Viewport,
} from "@xyflow/react";
import { createContext, useContext, useEffect, useMemo, useRef, useState, type Dispatch, type DragEvent, type SetStateAction } from "react";
import { DRAG_TYPE, dragging, QuickAdd, type Item } from "./Browser";
import { CompactBlock } from "./Compact";
import { wireMismatch, type PortSides } from "./ports";
import { usePortLayout } from "./portLayout";
import { Bindable, NumberOrFormula } from "./Variables";
import { isRef } from "./bindings";
import { RemoveIcon } from "./RemoveIcon";
import { connectionOf, nearestAccepting, SNAP, WireDragContext, type Anchor } from "./wireSnap";
import {
  absolute,
  Boundary,
  descendants,
  display,
  frameAround,
  GroupActions,
  GroupBox,
  isFrame,
  isLevel,
  levelPath,
  ordered,
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
 * `ports`: where the user moved its ports to (drag a port's name); absent: inputs left, outputs right.
 */
export type BlockData = { def: BlockDef; params: Record<string, ParamValue>; missing?: boolean; ports?: PortSides };
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

const portDef = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  def?.[side].find((p) => p.id === port);
const portType = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  portDef(def, port, side)?.type;

/** Generic node: everything it shows comes from the backend's BlockDef. */
function Block({ id, data, selected }: NodeProps<BlockNode>) {
  const { updateNodeData } = useReactFlow<BlockNode, Wire>();
  const states = useContext(RunStates);
  const active = states.active.has(id);
  const progress = states.progress[id];
  const { params, missing } = data;
  const def = inForm(data.def, params);
  const failed = states.failed.has(id);
  // A simple block draws as a small plate (#70) until double-clicked open; fold puts it back.
  const [expanded, setExpanded] = useState(false);
  const ports = usePortLayout({ id, def, sides: data.ports, save: (ports) => updateNodeData(id, { ports }), disabled: missing });
  if (def.view?.compact && !expanded && !missing && !hasStepPorts(def)) {
    return (
      <CompactBlock
        id={id}
        def={def}
        params={params}
        selected={!!selected}
        word={failed ? "failed" : active ? "running" : undefined}
        progress={progress}
        onExpand={() => setExpanded(true)}
        popover={def.params.map((p) => (
          <label key={p.id} className="param">
            <span>
              {p.id}
              {"unit" in p && p.unit && <span className="unit"> {p.unit}</span>}
            </span>
            <ParamInput def={p} value={params[p.id]} set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })} />
          </label>
        ))}
      />
    );
  }
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
      {ports.top}
      <div className="block-title">
        <span className="block-name" title={def.linked ? "Linked to a station object: set its properties, or remove it, on the Station" : undefined}>
          {missing ? def.type : def.label}
          {def.linked && <span className="linked-tag"> {id}</span>}
        </span>
        <span className={`block-tag tag-${tag}`}>{tag}</span>
        {def.view?.compact && (
          <button className="fold nodrag" title="Fold back to a plate" onClick={() => setExpanded(false)}>
            fold
          </button>
        )}
      </div>
      {active && progress !== undefined && progress < 1 && (
        <div className="block-progress" title={`${Math.round(progress * 100)} %`}>
          <div style={{ width: `${progress * 100}%` }} />
        </div>
      )}
      {ports.rows}
      {def.params.map((p) => (
        <label key={p.id} className="param">
          <span>
            {p.id}
            {"unit" in p && p.unit && <span className="unit"> {p.unit}</span>}
          </span>
          <ParamInput
            def={p}
            value={params[p.id]}
            disabled={missing}
            set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })}
          />
        </label>
      ))}
      {ports.bottom}
    </div>
  );
}

/**
 * A param's (or a station property's) field. It can be bound to a variable (#36): drop one
 * on it from the Variables sheet, or type = in a number field; a bound one shows as a chip.
 */
export function ParamInput(props: { def: ParamDef; value: ParamValue; set: (v: ParamValue) => void; disabled?: boolean }) {
  return (
    <Bindable def={props.def} value={props.value} set={props.set} disabled={props.disabled}>
      {isRef(props.value) ? null : <Field {...props} />}
    </Bindable>
  );
}

/** A number field, or for a `key` param a field that takes the next key you press. */
function Field({
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
    <NumberOrFormula def={def} value={Number(value)} set={set} disabled={disabled} />
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
/** The wires to follow (#55): those of the hovered wire, port or block. null: nothing is hovered. */
const Lit = createContext<ReadonlySet<string> | null>(null);

function WireEdge({ id, source, target, data, selected, markerEnd, ...geometry }: EdgeProps<Wire>) {
  const { deleteElements, getEdges } = useReactFlow();
  const hot = useContext(HotWire) === id;
  const lit = useContext(Lit);
  const follow = lit ? (lit.has(id) ? " lit" : " dim") : "";
  // Wires that leave one output start together: each bends a little differently, so they part at once.
  const siblings = getEdges().filter((w) => w.source === source && w.sourceHandle === geometry.sourceHandleId).map((w) => w.id).sort();
  const curvature = 0.25 + 0.14 * Math.max(0, siblings.indexOf(id));
  const step = data?.type === "step";
  const sourceBottom = useBottom(source);
  const targetBottom = useBottom(target);
  // Only a wire leaving to the right for a port facing left runs back round the blocks;
  // ports moved to other sides take an ordinary curve that leaves the way they face.
  const back = geometry.sourcePosition === Position.Right && geometry.targetPosition === Position.Left && geometry.targetX < geometry.sourceX + 8;
  const [path, x, y] = back
    ? loopPath(
        geometry.sourceX,
        geometry.sourceY,
        geometry.targetX,
        geometry.targetY,
        Math.max(sourceBottom ?? geometry.sourceY, targetBottom ?? geometry.targetY, geometry.sourceY, geometry.targetY) + 28,
      )
    : step
      ? getSmoothStepPath(geometry)
      : getBezierPath({ ...geometry, curvature });
  return (
    <>
      {/* Casing: a wider stroke in the canvas colour, so where wires cross, one is seen to pass over the other. */}
      <path d={path} className={`wire-casing${follow}`} />
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={`wire-${data?.type ?? "signal"}${hot ? " drop-target" : ""}${follow}`} />
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

const nodeTypes = { block: Block, frame: GroupBox, boundary: Boundary };
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
    const ports = n.ports ? { ports: n.ports } : {};
    if (def) {
      const node = makeNode(def, n.id, x, y, n.params);
      return { ...node, data: { ...node.data, ...ports } };
    }
    const node = makeNode(missingDef(n.type, n.params ?? {}, wires, n.id), n.id, x, y, n.params);
    return { ...node, data: { ...node.data, ...ports, missing: true } };
  });
  const byId = new Map<string, BlockDef>(nodes.map((n) => [n.id, inForm(n.data.def, n.data.params)]));
  // Groups round their blocks and the groups in them, innermost first: each is made while
  // everything it holds still has its place on the whole canvas.
  const groups = graph.groups ?? [];
  const parentOf = new Map(groups.flatMap((g) => (g.parent ? [[g.id, g.parent] as const] : [])));
  const depth = (id: string) => {
    let d = 0;
    for (let p = parentOf.get(id); p && d < 64; p = parentOf.get(p)) d++;
    return d;
  };
  let all: FlowNode[] = nodes;
  for (const g of [...groups].sort((a, b) => depth(b.id) - depth(a.id))) {
    const inside = all.filter((n) => !n.parentId && (isBlock(n) ? g.nodes.includes(n.id) : parentOf.get(n.id) === g.id));
    if (inside.length === 0) continue;
    const { frame, children } = frameAround(g.id, g.kind, inside, {
      label: g.label,
      collapsed: g.collapsed,
      seconds: g.params?.seconds,
      reset: g.params?.reset,
      pos: g.pos,
      sides: g.ports,
    });
    if (g.kind === "story") byId.set(g.id, STORY_DEF);
    const moved = new Map(children.map((c) => [c.id, c]));
    all = [...all.map((n) => moved.get(n.id) ?? n), frame];
  }
  // A frame's children are kept inside it, and so is a group in a frame.
  all = all.map((n) => {
    const p = n.parentId ? all.find((q) => q.id === n.parentId) : undefined;
    return p && isFrame(p) && p.data.kind === "visual" ? { ...n, extent: "parent" as const } : n;
  });
  const edges = wires.map((w) => makeWire(w, portType(byId.get(w.source), w.sourceHandle, "outputs") ?? "signal"));
  return { nodes: ordered(all), edges };
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
      return { ...n, deletable: !def.linked, data: { def, params: { ...defaults, ...kept }, ...ports } };
    }
    const wires = edges.map((e) => ({ ...e, type: e.data?.type }));
    return { ...n, deletable: true, data: { ...n.data, def: missingDef(n.data.def.type, n.data.params, wires, n.id, n.data.def), missing: true } };
  });
  return changed ? out : nodes;
}

export function toGraph(nodes: FlowNode[], edges: Wire[]): Graph {
  const groups = toGroups(nodes);
  return {
    nodes: nodes.filter(isBlock).map((n) => ({ id: n.id, type: n.data.def.type, params: n.data.params, ...(n.data.ports ? { ports: n.data.ports } : {}) })),
    edges: edges.map((e) => ({ from: [e.source, e.sourceHandle ?? ""], to: [e.target, e.targetHandle ?? ""] })),
    ...(groups.length > 0 ? { groups } : {}),
  };
}

/**
 * A new block at `at` (flow coordinates on the whole canvas, its top-left), selected and the
 * only one selected; inside subsystem `level` when one is open.
 */
export function withBlock(nodes: FlowNode[], def: BlockDef, at: { x: number; y: number }, level: string | null = null): [FlowNode[], string] {
  const id = freeId(def.type, nodes);
  const node = { ...inLevel(makeNode(def, id, Math.round(at.x), Math.round(at.y)), level, nodes), selected: true };
  return [[...nodes.map((n) => (n.selected ? { ...n, selected: false } : n)), node], id];
}

/** A new node placed in subsystem `level` (its position made relative to it), or as it is. */
function inLevel<N extends FlowNode>(node: N, level: string | null, nodes: FlowNode[]): N {
  const host = level ? nodes.find((n) => n.id === level) : undefined;
  if (!host) return node;
  const o = absolute(host, nodes);
  return { ...node, parentId: level, position: { x: node.position.x - o.x, y: node.position.y - o.y } };
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
  /** The subsystem or story open on the canvas (#68); null: the top level. */
  level: string | null;
  setLevel: (level: string | null) => void;
};

/** Blocks by palette group, groups in catalog order. */
export function FlowEditor({ blocks, nodes, edges, setNodes, setEdges, onNodesChange, onEdgesChange, minimap, level: wanted, setLevel }: Props) {
  const states = useContext(RunStates);
  // The open level, if it is still there (undo or ungroup may have taken it away).
  const level = wanted && isLevel(nodes.find((n) => n.id === wanted)) ? wanted : null;
  useEffect(() => {
    if (wanted !== level) setLevel(level);
  }, [wanted, level, setLevel]);
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

  // A wire joins ports of one type (signal to signal, step to step; any to anything but a
  // step), in one unit when both ends have one (#4). The engine checks again.
  const isValidConnection: IsValidConnection<Wire> = (shown) => {
    const c = real(shown as Connection);
    const from = portDef(defOf(c.source), c.sourceHandle, "outputs");
    const to = portDef(defOf(c.target), c.targetHandle, "inputs");
    return c.source !== c.target && !!from && !!to && !wireMismatch(from, to);
  };

  const onConnect = (shown: Connection) => {
    const c = real(shown);
    setEdges((ws) => connect(ws, c, wireType(c)));
  };

  // ---- Levels (#68): a subsystem or story opens on the canvas; the way back is the breadcrumb ----
  const { getViewport, setViewport, fitView } = useReactFlow();
  const views = useRef(new Map<string | null, Viewport>()); // each level keeps its own view
  const deselectAll = () => {
    setNodes((ns) => (ns.some((n) => n.selected) ? ns.map((n) => (n.selected ? { ...n, selected: false } : n)) : ns));
    setEdges((ws) => (ws.some((w) => w.selected) ? ws.map((w) => (w.selected ? { ...w, selected: false } : w)) : ws));
  };
  const go = (to: string | null) => {
    if (to === level) return;
    views.current.set(level, getViewport());
    deselectAll();
    setLevel(to);
  };
  const first = useRef(true);
  useEffect(() => {
    if (first.current) {
      first.current = false; // the app fits the view when a graph loads
      return;
    }
    const saved = views.current.get(level);
    const t = window.setTimeout(() => (saved ? setViewport(saved) : fitView({ padding: 0.25, duration: 200, maxZoom: 1.2 })), 30);
    return () => window.clearTimeout(t);
  }, [level]); // eslint-disable-line react-hooks/exhaustive-deps -- on entering or leaving only
  const path = levelPath(level, nodes);
  const up = path.length > 1 ? path[path.length - 2].id : null;
  // Alt+↑ goes up a level; so does Escape when nothing is selected.
  useEffect(() => {
    if (!level) return;
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable)) return;
      const escape = e.key === "Escape" && !nodes.some((n) => n.selected) && !edges.some((w) => w.selected);
      if (!escape && !(e.altKey && e.key === "ArrowUp")) return;
      e.preventDefault();
      go(up);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });

  // Grouping: a subsystem, story or frame round the selected blocks and groups of this
  // level, which must sit in the same place (not some in a frame and some out). A story
  // holds signal blocks only: it is itself the step. A frame holds blocks only.
  const choosable = (ns: FlowNode[], shownIds: Set<string>) => ns.filter((n) => n.selected && shownIds.has(n.id));
  const runStates = useContext(RunStates);
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
  // A group is as busy as its busiest block: failed, else running.
  const inside = useMemo(() => {
    const m = new Map<string, string[]>();
    for (const n of nodes) if (isLevel(n)) m.set(n.id, descendants(n.id, nodes));
    return m;
  }, [nodes]);
  const stateOf = (id: string) => {
    const all = [id, ...(inside.get(id) ?? [])];
    return all.some((x) => runStates.failed.has(x)) ? ("failed" as const) : all.some((x) => runStates.active.has(x)) ? ("running" as const) : undefined;
  };
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
  const shown = display(nodes, edges, (node, port, side) => portType(defOf(node), port, side) ?? "signal", level);
  const shownIds = new Set(shown.nodes.filter((n) => !n.hidden).map((n) => n.id));
  const picked = choosable(nodes, shownIds);
  const sameParent = picked.length > 0 && picked.every((n) => n.parentId === picked[0].parentId);
  const canGroup = sameParent;
  const canFrame = sameParent && picked.every(isBlock);
  const canStory = canFrame && picked.every((n) => isBlock(n) && !hasStepPorts(portsOf(n)));
  const group = (kind: GroupData["kind"]) => {
    const ids = new Set(picked.map((n) => n.id));
    setNodes((ns) => {
      const chosen = ns.filter((n) => ids.has(n.id));
      if (chosen.length === 0) return ns;
      const host = chosen[0].parentId ? ns.find((n) => n.id === chosen[0].parentId) : undefined;
      const inFrame = !!host && isFrame(host) && host.data.kind === "visual";
      const { frame, children } = frameAround(freeId(kind === "visual" ? "frame" : kind, ns), kind, chosen, { inFrame });
      const moved = new Map(children.map((c) => [c.id, c]));
      return ordered([...ns.map((n) => moved.get(n.id) ?? n), { ...frame, selected: kind !== "visual" }]);
    });
  };

  // Dragging a wire's end onto another port moves it; dropping it anywhere else deletes it.
  const moved = useRef(true);
  const onReconnect = (old: Wire, shownC: Connection) => {
    moved.current = true;
    const c = real(shownC);
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
  const { screenToFlowPosition, deleteElements } = useReactFlow();

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
      const [next, made] = withBlock(ns, def, { x: at.x - BLOCK_SIZE.w / 2, y: at.y - 16 }, level);
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

  // ---- Several at once: dragging on empty canvas draws a box; a dialog acts on it (#54) ----
  const [busy, setBusy] = useState(false); // dragging blocks or the box: the dialog waits
  const pickedNodes = shown.nodes.filter((n) => n.selected && !n.hidden);
  const pickedWires = shown.edges.filter((w) => w.selected);
  const pickedFrames = pickedNodes.filter(isFrame);
  const kept = pickedNodes.filter((n) => isBlock(n) && n.data.def.linked).length; // linked blocks aren't deleted
  const many = pickedNodes.length > 0; // a block or group, with or without wires (wires alone have ×)
  // The dialog floats above the selection, or below it when the selection is at the top of
  // the canvas (above, it would sit under the panel's head, out of reach).
  const pickedIds = pickedNodes.map((n) => n.id).join(",");
  const roomAbove = useStore((st) => {
    const [, ty, zoom] = st.transform;
    let top = Infinity;
    for (const id of pickedIds ? pickedIds.split(",") : []) {
      const n = st.nodeLookup.get(id);
      if (n) top = Math.min(top, n.internals.positionAbsolute.y * zoom + ty);
    }
    return top > 64;
  });
  const deselect = () => {
    setNodes((ns) => ns.map((n) => (n.selected ? { ...n, selected: false } : n)));
    setEdges((ws) => ws.map((w) => (w.selected ? { ...w, selected: false } : w)));
  };
  const removePicked = () =>
    deleteElements({ nodes: pickedNodes.map((n) => ({ id: n.id })), edges: pickedWires.map((w) => ({ id: w.id })) });
  useEffect(() => {
    if (!many) return;
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (e.key !== "Escape" || (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT"))) return;
      e.preventDefault(); // not a key for the backend
      deselect();
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  });
  const counted = (n: number, one: string) => (n ? [`${n} ${one}${n === 1 ? "" : "s"}`] : []);
  const summary = [
    ...counted(pickedNodes.filter(isBlock).length, "block"),
    ...counted(pickedFrames.length, "group"),
    ...counted(pickedWires.length, "wire"),
  ].join(" · ");

  // A dragged card: plan what dropping it here would do, show that, and do it on drop.
  const [hint, setHint] = useState<{ plan: Plan; x: number; y: number; label: string } | null>(null);
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
  const box = wrap.current?.getBoundingClientRect();
  const rel = (x: number, y: number) => ({ x: x - (box?.left ?? 0), y: y - (box?.top ?? 0) });

  return (
    <GroupActions.Provider value={{ ...actions, enter: go, stateOf }}>
    <HotWire.Provider value={hint?.plan.kind === "insert" ? hint.plan.wire.id : null}>
    <Lit.Provider value={lit}>
    <div className="flow">
      <div
        className="flow-canvas"
        ref={wrap}
        onMouseOver={follow}
        onMouseMove={(e) => (pointer.current = { x: e.clientX, y: e.clientY })}
        onMouseLeave={() => {
          pointer.current = null;
          setLit(null);
        }}
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
          selectionKeyCode={["Control", "Meta"]} // Ctrl-drag on the canvas draws a box…
          selectionMode={SelectionMode.Partial} // …and picks every block it touches; a plain drag pans
          panActivationKeyCode={null} // Space belongs to the backend (Key blocks)
          onNodeDragStart={() => setBusy(true)}
          onNodeDragStop={() => setBusy(false)}
          onSelectionDragStart={() => setBusy(true)}
          onSelectionDragStop={() => setBusy(false)}
          onSelectionStart={() => setBusy(true)}
          onSelectionEnd={() => setBusy(false)}
          zoomOnDoubleClick={false} // double-click is quick add
          fitView
        >
          <Background />
          {level && (
            <Panel position="top-left" className="crumbs" aria-label="Where you are: click a level to go back up (Alt+↑ or Escape goes up one)">
              <button className="crumb" onClick={() => go(null)}>
                Flow
              </button>
              {path.map((g) => (
                <span key={g.id} className="crumb-step">
                  <span className="crumb-sep">›</span>
                  {g.id === level ? (
                    <input
                      className="crumb-here nodrag"
                      value={g.data.label}
                      size={Math.max(4, g.data.label.length)}
                      title={`Rename this ${g.data.kind}`}
                      onChange={(e) => actions.update(g.id, { label: e.target.value })}
                    />
                  ) : (
                    <button className="crumb" onClick={() => go(g.id)}>
                      {g.data.label}
                    </button>
                  )}
                </span>
              ))}
              <span className="crumb-kind">{path[path.length - 1]?.data.kind}</span>
            </Panel>
          )}
          {minimap && (
            <MiniMap
              pannable
              zoomable
              ariaLabel="Overview of the flow: click or drag to move the view"
              // Words, not colours, tell kinds apart (DESIGN.md): grey blocks, groups as outlines,
              // the signal colour for running, red for failed.
              nodeColor={(n) => (n.type === "frame" && (n.data as GroupData).kind === "visual" ? "transparent" : states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeColor={(n) => (states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeWidth={3}
              nodeBorderRadius={2}
              maskColor="var(--surface-float)"
              maskStrokeColor="var(--line-ink)"
              maskStrokeWidth={1}
            />
          )}
          <NodeToolbar
            nodeId={pickedNodes.map((n) => n.id)}
            isVisible={many && !busy}
            position={roomAbove ? Position.Top : Position.Bottom}
            offset={14}
            className="selection-dialog nodrag nopan nowheel"
          >
            <span className="lbl">{summary}</span>
            <button
              className="key small"
              disabled={kept === pickedNodes.length && pickedWires.length === 0}
              title="Delete the selection (Delete)"
              onClick={removePicked}
            >
              Delete
            </button>
            <button
              className="key small"
              disabled={!canGroup}
              title={canGroup ? "Make the selection a subsystem: one block here, its insides on their own level (double-click it to open)" : "Pick blocks or groups in the same place (not some in a frame)"}
              onClick={() => group("subsystem")}
            >
              Group
            </button>
            <button
              className="key small"
              disabled={!canStory}
              title={canStory ? "Make the selected blocks a story: one step of a Step flow" : "A story holds signal blocks only: no step ports"}
              onClick={() => group("story")}
            >
              Story
            </button>
            <button className="key small" disabled={!canFrame} title="Draw a frame round the selected blocks (tidy; they stay on this level and run as before)" onClick={() => group("visual")}>
              Frame
            </button>
            <button
              className="key small"
              disabled={pickedFrames.length === 0}
              title="Ungroup: put the selected groups' blocks back on this level"
              onClick={() => pickedFrames.forEach((f) => actions.ungroup(f.id))}
            >
              Ungroup
            </button>
            {kept > 0 && (
              <span className="lbl kept" title="Linked blocks come from the backend's own graph">
                {kept} linked {kept === 1 ? "block stays" : "blocks stay"}
              </span>
            )}
          </NodeToolbar>
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
        <span className="kbd">/</span> or double-click the canvas adds a block. Click a block, or Ctrl-drag a box (Ctrl-click
        adds one): the dialog above it deletes, groups or makes a story. Drag the canvas to pan; the wheel zooms. Drag a
        wire's end off its port to remove it. Drag a port's name to another side of its block (double-click puts it back). Group makes a
        subsystem: double-click it to open, Alt+↑ or Escape to come back; ▾ collapses a group. Ctrl+Z undoes, Ctrl+Shift+Z redoes.
      </p>
    </div>
    </Lit.Provider>
    </HotWire.Provider>
    </GroupActions.Provider>
  );
}

const hasStepPorts = (def: BlockDef) => [...def.inputs, ...def.outputs].some((p) => p.type === "step");
