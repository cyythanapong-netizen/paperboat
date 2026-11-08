import {
  addEdge,
  Background,
  BaseEdge,
  EdgeLabelRenderer,
  MarkerType,
  MiniMap,
  NodeToolbar,
  Panel,
  useStore,
  Position,
  ReactFlow,
  SelectionMode,
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
  useUpdateNodeInternals,
} from "@xyflow/react";
import { createContext, Fragment, useContext, useEffect, useId, useMemo, useRef, useState, type Dispatch, type DragEvent, type MutableRefObject, type SetStateAction } from "react";
import { DRAG_TYPE, dragging, QuickAdd, type Item } from "./Browser";
import { ownKeys } from "./commands";
import { CompactBlock } from "./Compact";
import { copySelection, pasteClip, type Clip } from "./clipboard";
import { connectError, iconEnd, inForm, netWireType, placePorts, SIDES, signalEnd, signOf, wireMismatch, type PortSides, type WireEnd } from "./ports";
import { flipSideX, flipSideY, iconValid, placeIconPorts, rotateIcon, rotateSideCCW, rotateSideCW, useCanvasHotkeys, type RotationDeg } from "@paperboat/shared";
import { usePortLayout } from "./portLayout";
import { Bindable, draggedVariable, NumberOrFormula, VAR_DRAG, VarsContext } from "./Variables";
import { isRef } from "./bindings";
import { RemoveIcon } from "./RemoveIcon";
import { connectionOf, nearestAccepting, SNAP, WireDragContext, type Anchor } from "./wireSnap";
import { arrangeFlowNodes, canArrangeFlow } from "./flowLayout";
import { middleOfWire, orthoPoints, roundedPolyline, routeWires, squareUp, type Obstacle, type RouteInput, type WireRoute, type WirePoint } from "./wireRouting.ts";
import { useWireBends } from "@paperboat/shared/components";
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
  type Expose,
  type GroupData,
  type GroupNode,
} from "./groups";
import type { BlockDef, Connectors, Graph, ObjectDef, ParamDef, ParamValue, PortDef, Station } from "./protocol";
import { PageIcon } from "./Shell";

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

export { inForm };


/** A block's ports as they are now (a magic block's: its form's). */
export const portsOf = (n: BlockNode) => inForm(n.data.def, n.data.params);

const portDef = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  def?.[side].find((p) => p.id === port);
/**
 * The end of a wire at port `port` of `def`: an icon connector (its class from the catalog, either
 * direction for an acausal one), else a legacy signal port on the `side` the wire runs from.
 */
const wireEnd = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs", connectors: Connectors): WireEnd | null => {
  const ip = iconValid(def?.view?.icon) ? def?.view?.icon?.ports?.find((p) => p.id === port) : undefined;
  if (ip) return iconEnd(ip, connectors);
  return portDef(def, port, side) ? signalEnd(side === "inputs" ? "in" : "out") : null;
};

/** What the canvas knows of icon ports: the catalog's connectors, wires per port, ports whose wires dangle (`if` false). */
const ConnectorsContext = createContext<Connectors>({});
const IconWires = createContext<{ count: ReadonlyMap<string, Record<string, number>>; missing: ReadonlySet<string> }>({ count: new Map(), missing: new Set() });

const portType = (def: BlockDef | undefined, port: string | null | undefined, side: "inputs" | "outputs") =>
  portDef(def, port, side)?.type;

/** Generic node: everything it shows comes from the backend's BlockDef. */
function Block({ id, data, selected }: NodeProps<BlockNode>) {
  const { updateNodeData } = useReactFlow<BlockNode, Wire>();
  const states = useContext(RunStates);
  const active = states.active.has(id);
  const progress = states.progress[id];
  const { params, missing, commented, commentMode, comment } = data;
  const def = inForm(data.def, params);
  const failed = states.failed.has(id);
  const connectors = useContext(ConnectorsContext);
  const iconWires = useContext(IconWires);
  const [noteOpen, setNoteOpen] = useState(false);
  // Ports that follow a param come and go: have the canvas measure the handles again.
  const updateInternals = useUpdateNodeInternals();
  const portKey = [...def.inputs, ...def.outputs].map((p) => p.id).join(",");
  useEffect(() => updateInternals(id), [portKey, id, updateInternals, data.rotation, data.mirror]);
  // A simple block draws as a small plate (#70) until double-clicked open; fold puts it back.
  const [expanded, setExpanded] = useState(false);
  const ports = usePortLayout({
    id,
    def,
    sides: data.ports,
    save: (ports) => updateNodeData(id, { ports }),
    disabled: missing,
    sign: (p, dir) => (dir === "in" ? signOf(def, params, def.inputs.indexOf(p)) : ""),
  });
  const placedIcon = (data.rotation || data.mirror) && def.view?.icon ? rotateIcon(def.view.icon, { rotation: data.rotation, mirror: data.mirror }) : def.view?.icon;
  const viewDef = placedIcon ? { ...def, view: { ...def.view, icon: placedIcon } } : def;
  if ((def.view?.compact || iconValid(def.view?.icon)) && !expanded && !missing && !hasStepPorts(def)) {
    return (
      <CompactBlock
        id={id}
        def={viewDef}
        params={params}
        selected={!!selected}
        commented={commented}
        word={failed ? "failed" : active ? "running" : undefined}
        progress={progress}
        onExpand={() => setExpanded(true)}
        connectors={connectors}
        wires={iconWires.count.get(id)}
        popover={def.params.map((p) => (
          <ParamRow key={p.id} def={p} value={params[p.id]} set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })} />
        ))}
      />
    );
  }
  const step = hasStepPorts(def);
  const mode = commentMode ?? "out";
  const classes = [
    "block",
    `kind-${def.kind}`,
    step && "step",
    selected && "selected",
    active && "active",
    def.linked && "linked",
    missing && "missing",
    failed && "failed",
    commented && "commented",
    commented && mode === "through" && "comment-through",
  ];
  // The head's tag says what is happening to the block, else what it is: words, not colours.
  const tag = commented
    ? mode === "through"
      ? "// bypass"
      : "// commented"
    : missing
      ? "missing"
      : failed
        ? "failed"
        : active
          ? "running"
          : def.linked
            ? "linked"
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
        <span className="block-name" title={def.linked ? `Linked to station object ${id}: set its properties, or remove it, on the Station` : undefined}>
          {missing ? def.type : def.label}
          {def.linked && <span className="linked-tag"> <PageIcon page="station" className="link-mark" />{id}</span>}
        </span>
        <button
          type="button"
          className={`block-note-btn nodrag ${comment ? "has-note" : ""}`}
          title={comment ? `Note: ${comment}` : "Add note"}
          onClick={() => setNoteOpen((o) => !o)}
        >
          {comment ? "note*" : "note"}
        </button>
        <span className={`block-tag tag-${tag}`}>{tag}</span>
        {def.view?.compact && (
          <button className="fold nodrag" title="Fold back to a plate" onClick={() => setExpanded(false)}>
            fold
          </button>
        )}
      </div>
      {noteOpen && (
        <div className="block-note-well nodrag nowheel nopan">
          <textarea
            className="block-note-input"
            placeholder="Notes or formulas..."
            value={comment ?? ""}
            onChange={(e) => updateNodeData(id, { comment: e.target.value })}
          />
        </div>
      )}
      {active && progress !== undefined && progress < 1 && (
        <div className="block-progress" title={`${Math.round(progress * 100)} %`}>
          <div style={{ width: `${progress * 100}%` }} />
        </div>
      )}
      {ports.rows}
      {def.params.map((p) => (
        <ParamRow
          key={p.id}
          def={p}
          value={params[p.id]}
          disabled={missing}
          set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })}
        />
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

/** A param (or station property) as a row: its id and unit, then its field. */
export function ParamRow(props: Parameters<typeof ParamInput>[0]) {
  const { def } = props;
  return (
    <label className="param">
      <span>
        {def.id}
        {"unit" in def && def.unit && <span className="unit"> {def.unit}</span>}
      </span>
      <ParamInput {...props} />
    </label>
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
  if (def.type === "variable") return <VariableName value={String(value)} set={set} disabled={disabled} />;
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

/** A `variable` param: the name of a project variable, typed or picked from the list. A variable dragged from the sheet can be dropped on it. */
function VariableName({ value, set, disabled }: { value: string; set: (v: ParamValue) => void; disabled?: boolean }) {
  const vars = useContext(VarsContext);
  const list = useId();
  const unknown = !!value && !!vars && !vars.list.some((v) => v.name === value);
  return (
    <>
      <input
        className="nodrag text-param"
        list={list}
        disabled={disabled}
        value={value}
        title={unknown ? `No variable called "${value}"` : "A project variable"}
        aria-invalid={unknown}
        onChange={(e) => set(e.target.value)}
        onDragOver={(e) => e.dataTransfer.types.includes(VAR_DRAG) && !disabled && e.preventDefault()}
        onDrop={(e) => {
          const name = draggedVariable();
          if (!name || disabled) return;
          e.preventDefault();
          set(name);
        }}
      />
      <datalist id={list}>
        {vars?.list.map((v) => (
          <option key={v.name} value={v.name} />
        ))}
      </datalist>
    </>
  );
}

/** The wire a dragged card would be inserted into, if any (drawn thicker). */
const HotWire = createContext<string | null>(null);
/** The wires to follow (#55): those of the hovered wire, port or block. null: nothing is hovered. */
const Lit = createContext<ReadonlySet<string> | null>(null);
/** Whole-canvas routed wire geometries computed in one pass. */
const WireRoutes = createContext<Map<string, WireRoute> | null>(null);
const CommentedOutContext = createContext<ReadonlySet<string>>(new Set());
/** Save a wire's dragged bends; null when the canvas is locked (wires don't move). */
const SetBends = createContext<((id: string, bends: WirePoint[]) => void) | null>(null);

const sideOf = (p: Position): "left" | "right" | "top" | "bottom" => (p === Position.Left ? "left" : p === Position.Top ? "top" : p === Position.Bottom ? "bottom" : "right");

function WireEdge({ id, data, selected, markerEnd, ...geometry }: EdgeProps<Wire>) {
  const { deleteElements } = useReactFlow();
  const hot = useContext(HotWire) === id;
  const lit = useContext(Lit);
  const follow = lit ? (lit.has(id) ? " lit" : " dim") : "";
  const routes = useContext(WireRoutes);
  const route = routes?.get(id);
  const commentedOut = useContext(CommentedOutContext);
  const wireCommented = commentedOut.has(geometry.source) || commentedOut.has(geometry.target);
  const missing = useContext(IconWires).missing;
  const commentedClass = (wireCommented ? " wire-commented" : "") + (missing.has(`${geometry.source}:${geometry.sourceHandleId}`) || missing.has(`${geometry.target}:${geometry.targetHandleId}`) ? " wire-missing" : "");

  const setBends = useContext(SetBends);
  // The ports as the router saw them, else as React Flow does.
  const from: WirePoint = route ? route.points[0] : [geometry.sourceX, geometry.sourceY];
  const to: WirePoint = route ? route.points[route.points.length - 1] : [geometry.targetX, geometry.targetY];
  const routed = data?.points
    ? squareUp(from, data.points, to)
    : (route?.points ?? orthoPoints(from, sideOf(geometry.sourcePosition), to, sideOf(geometry.targetPosition)));
  const { points, handles } = useWireBends(routed, !!selected && !!setBends, (bends) => setBends?.(id, bends));
  const path = roundedPolyline(points, 8);
  const [x, y] = route && !data?.points && points === routed ? [route.labelX, route.labelY] : middleOfWire(points);
  return (
    <>
      {/* Casing: a wider stroke in the canvas colour, so where wires cross, one is seen to pass over the other. */}
      <path d={path} className={`wire-casing${follow}${commentedClass}`} />
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={`wire-${data?.type ?? "signal"}${hot ? " drop-target" : ""}${follow}${commentedClass}`} />
      {/* What a dropped card finds under the pointer: a wide, invisible stroke tagged with the wire. */}
      <path d={path} className="wire-hit" data-wire={id} />
      {selected && (
        <EdgeLabelRenderer>
          <button
            className="remove nodrag nopan"
            title="Delete wire"
            style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y - 16}px)` }}
            onClick={() => deleteElements({ edges: [{ id }] })}
          >
            <RemoveIcon />
          </button>
        </EdgeLabelRenderer>
      )}
      {handles}
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
    const rotation = n.rotation !== undefined ? { rotation: n.rotation } : {};
    const mirror = n.mirror !== undefined ? { mirror: n.mirror } : {};
    if (def) {
      const node = makeNode(def, n.id, x, y, n.params);
      return {
        ...node,
        data: {
          ...node.data,
          ...ports,
          ...rotation,
          ...mirror,
          ...(n.commented !== undefined ? { commented: n.commented } : {}),
          ...(n.comment_mode !== undefined ? { commentMode: n.comment_mode } : {}),
          ...(n.comment !== undefined ? { comment: n.comment } : {}),
        },
      };
    }
    const node = makeNode(missingDef(n.type, n.params ?? {}, wires, n.id), n.id, x, y, n.params);
    return {
      ...node,
      data: {
        ...node.data,
        ...ports,
        ...rotation,
        ...mirror,
        missing: true,
        ...(n.commented !== undefined ? { commented: n.commented } : {}),
        ...(n.comment_mode !== undefined ? { commentMode: n.comment_mode } : {}),
        ...(n.comment !== undefined ? { comment: n.comment } : {}),
      },
    };
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
      release: g.release,
      pos: g.pos,
      sides: g.ports,
      expose: g.expose?.map((x) => ({ side: x.side, to: [x.to[0], x.to[1]] as [string, string] })),
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
  const edges = wires.map((w, i) => {
    const wire = makeWire(w, portType(byId.get(w.source), w.sourceHandle, "outputs") ?? "signal");
    const points = graph.edges[i].points;
    return points ? { ...wire, data: { type: wire.data!.type, points } } : wire;
  });
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

export function toGraph(nodes: FlowNode[], edges: Wire[]): Graph {
  const groups = toGroups(nodes);
  return {
    nodes: nodes.filter(isBlock).map((n) => ({
      id: n.id,
      type: n.data.def.type,
      params: n.data.params,
      ...(n.data.ports ? { ports: n.data.ports } : {}),
      ...(n.data.rotation !== undefined ? { rotation: n.data.rotation } : {}),
      ...(n.data.mirror !== undefined ? { mirror: n.data.mirror } : {}),
      ...(n.data.commented !== undefined ? { commented: n.data.commented } : {}),
      ...(n.data.commentMode !== undefined ? { comment_mode: n.data.commentMode } : {}),
      ...(n.data.comment !== undefined ? { comment: n.data.comment } : {}),
    })),
    edges: edges.map((e) => ({ from: [e.source, e.sourceHandle ?? ""], to: [e.target, e.targetHandle ?? ""] })),
    ...(groups.length > 0 ? { groups } : {}),
  };
}

/** `toGraph` and every block's place on the whole canvas, for a project file (a run doesn't need places). */
export function toFlow(nodes: FlowNode[], edges: Wire[]): Graph {
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const g = toGraph(nodes, edges);
  return {
    ...g,
    nodes: g.nodes.map((n) => {
      const at = absolute(byId.get(n.id)!, byId);
      return { ...n, pos: [Math.round(at.x), Math.round(at.y)] };
    }),
    edges: g.edges.map((e, i) => (edges[i].data?.points ? { ...e, points: edges[i].data!.points } : e)),
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
  /** The catalog's connector classes (icon ports, §4). */
  connectors?: Connectors;
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
  /** A run locks arranging: the station and its linked blocks must stay put. */
  locked: boolean;
  /** Filled with what the menus call: the same actions as the keys and the selection dialog. */
  controls?: MutableRefObject<FlowControls | null>;
};

/**
 * The canvas's actions for the menus (#78). `can` says what the selection allows now; it is read
 * when a menu opens, so `paste` looks at the clipboard then.
 */
export type FlowControls = {
  copy: () => void;
  cut: () => void;
  paste: () => void;
  remove: () => void;
  group: (kind: GroupData["kind"]) => void;
  ungroup: () => void;
  /** Arrange this level and its contents; false when locked or already arranged. */
  arrange: () => boolean;
  comment: (mode?: "out" | "through") => void;
  rotate: (cw?: boolean) => void;
  flip: (axis?: "x" | "y") => void;
  duplicate: () => void;
  can: {
    copy: boolean;
    paste: boolean;
    remove: boolean;
    group: boolean;
    frame: boolean;
    story: boolean;
    ungroup: boolean;
    arrange: boolean;
    comment: boolean;
    rotate: boolean;
    flip: boolean;
    duplicate: boolean;
  };
};

const NO_CONNECTORS: Connectors = {};

/** Blocks by palette group, groups in catalog order. */
export function FlowEditor({ blocks, connectors = NO_CONNECTORS, nodes, edges, setNodes, setEdges, onNodesChange, onEdgesChange, minimap, level: wanted, setLevel, locked, controls }: Props) {
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
  const wireType = (c: Connection | Wire) => {
    const end = wireEnd(defOf(c.source), c.sourceHandle, "outputs", connectors);
    return end && end.kind === "acausal" ? netWireType(connectors[end.cls]) : (portType(defOf(c.source), c.sourceHandle, "outputs") ?? "signal");
  };

  // A wire joins ports of one type (signal to signal, step to step; any to anything but a
  // step), in one unit when both ends have one (#4). The engine checks again.
  const isValidConnection: IsValidConnection<Wire> = (shown) => {
    const c = real(shown as Connection);
    return c.source !== c.target && !refusal(c);
  };
  // Why a wire may not join, in words (null if it may): icon connectors by §4, plain ports by type and unit.
  const refusal = (c: Connection): string | null => {
    const [a, b] = [defOf(c.source), defOf(c.target)];
    const [ea, eb] = [wireEnd(a, c.sourceHandle, "outputs", connectors), wireEnd(b, c.targetHandle, "inputs", connectors)];
    if (!ea || !eb) return "that port is not there";
    const bad = connectError(ea, eb);
    if (bad) return bad;
    const from = portDef(a, c.sourceHandle, "outputs");
    const to = portDef(b, c.targetHandle, "inputs");
    return from && to ? wireMismatch(from, to) : null;
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
      if (ownKeys(t) || (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable))) return;
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

  // ---- Copy, cut and paste: Ctrl+C / X / V (⌘ on a Mac) on the Flow page, except while typing ----
  const store = useStoreApi();
  const copied = useRef<{ clip: Clip; pastes: number } | null>(null);
  const latest = useRef({ nodes, edges, level });
  latest.current = { nodes, edges, level };
  // One state update makes the whole recursive arrangement one undo step (#103).
  const arrange = (): boolean => {
    if (locked) return false;
    const current = latest.current;
    const next = arrangeFlowNodes(current.nodes, current.edges, current.level, (nodeId, handleId, dir) => {
      const n = store.getState().nodeLookup.get(nodeId);
      const bounds = n?.internals.handleBounds;
      const list = dir === "source" ? bounds?.source : bounds?.target;
      const h = list?.find((x) => (x.id ?? null) === (handleId ?? null));
      return h ? h.y + h.height / 2 : undefined;
    });
    if (next === current.nodes) return false;
    latest.current = { ...current, nodes: next };
    setNodes(next);
    window.setTimeout(() => fitView({ padding: 0.2, duration: 200, maxZoom: 1.2 }), 30);
    return true;
  };
  // The three actions, shared by the keys below and the Edit menu. Each says whether it did anything.
  const copySelected = (cut: boolean): boolean => {
    const { nodes: ns, edges: es } = latest.current;
    const clip = copySelection(ns, es);
    if (!clip) return false;
    copied.current = { clip, pastes: 0 };
    if (cut) {
      const gone = new Set(clip.nodes.map((n) => n.id));
      setNodes(ns.filter((n) => !gone.has(n.id)));
      setEdges(es.filter((w) => !gone.has(w.source) && !gone.has(w.target)));
    }
    return true;
  };
  const pasteCopied = (): boolean => {
    const c = copied.current;
    if (!c) return false;
    c.pastes += 1;
    const next = pasteClip(c.clip, latest.current.nodes, latest.current.edges, 30 * c.pastes);
    setNodes(next.nodes);
    setEdges(next.edges);
    return true;
  };
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey || e.defaultPrevented) return;
      const key = e.key.toLowerCase();
      if (key !== "c" && key !== "x" && key !== "v") return;
      const t = e.target as HTMLElement | null;
      if (t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement || t instanceof HTMLSelectElement || t?.isContentEditable) return;
      const view = store.getState().domNode;
      if (!view || view.offsetParent === null) return; // another page is showing
      if (key !== "v" && window.getSelection()?.toString()) return; // text selected: the browser's copy
      if (key === "v" ? pasteCopied() : copySelected(key === "x")) e.preventDefault();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [store, setNodes, setEdges]); // eslint-disable-line react-hooks/exhaustive-deps -- the two actions read refs

  // ---- Dragging a wire (#52): the port it would join is raised, ports that can't take it dim ----
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
        why: (nodeId: string, handleId: string | null, side: "source" | "target") =>
          side !== anchor.type && nodeId !== anchor.nodeId ? refusal(real(connectionOf(anchor, nodeId, handleId))) : null,
      }
    : null;
  // Icon ports: wires per port (3 or more draw a junction dot) and ports hidden by their `if`, whose wires stay as missing.
  const iconWires = useMemo(() => {
    const count = new Map<string, Record<string, number>>();
    for (const w of edges)
      for (const [node, handle] of [[w.source, w.sourceHandle], [w.target, w.targetHandle]] as const) {
        if (!handle) continue;
        const c = count.get(node) ?? {};
        c[handle] = (c[handle] ?? 0) + 1;
        count.set(node, c);
      }
    const dangling = new Set<string>();
    for (const n of nodes) {
      if (!isBlock(n) || !iconValid(n.data.def.view?.icon)) continue;
      for (const p of placeIconPorts(n.data.def.view!.icon!, n.data.params)) if (p.hidden) dangling.add(`${n.id}:${p.port.id}`);
    }
    return { count, missing: new Set([...dangling].filter((k) => edges.some((w) => `${w.source}:${w.sourceHandle}` === k || `${w.target}:${w.targetHandle}` === k))) };
  }, [nodes, edges]);
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

  // ---- Several at once: dragging on empty canvas draws a box; a dialog acts on it (#54) ----
  const [busy, setBusy] = useState(false); // dragging blocks or the box: the dialog waits
  const pickedNodes = shown.nodes.filter((n) => n.selected && !n.hidden);
  const pickedBlocks = pickedNodes.filter(isBlock);
  const pickedWires = shown.edges.filter((w) => w.selected);
  const pickedFrames = pickedNodes.filter(isFrame);
  // One block picked inside an open subsystem or story: its ports can be exposed on the group.
  const levelGroup = level ? nodes.find((n): n is GroupNode => n.id === level && isFrame(n)) : undefined;
  const pickedBlock = pickedNodes.length === 1 && isBlock(pickedNodes[0]) ? pickedNodes[0] : undefined;
  const kept = pickedNodes.filter((n) => isBlock(n) && n.data.def.linked).length; // linked blocks aren't deleted
  const many = pickedNodes.length > 0; // a block or group, with or without wires (wires alone have ×)
  const allCommented = pickedBlocks.length > 0 && pickedBlocks.every((b) => b.data.commented);

  const toggleComment = (mode: "out" | "through" = "out") => {
    if (pickedBlocks.length === 0) return;
    const shouldComment = !allCommented || (mode === "through" && pickedBlocks.some((b) => b.data.commentMode !== "through"));
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) =>
      ns.map((n) => {
        if (!ids.has(n.id) || !isBlock(n)) return n;
        return {
          ...n,
          data: {
            ...n.data,
            commented: shouldComment,
            commentMode: shouldComment ? mode : undefined,
          },
        };
      }),
    );
  };

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

  const updateNodeInternals = useUpdateNodeInternals();

  const rotateSelected = (cw: boolean) => {
    if (pickedBlocks.length === 0) return;
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) =>
      ns.map((n) => {
        if (!ids.has(n.id) || !isBlock(n)) return n;
        const currentRot = (n.data.rotation ?? 0) as RotationDeg;
        const nextRot = (((currentRot + (cw ? 270 : 90)) % 360) as RotationDeg);
        const def = inForm(n.data.def, n.data.params);
        const placed = placePorts(def, n.data.ports);
        const nextPorts: PortSides = {};
        for (const side of SIDES) {
          const nextSide = cw ? rotateSideCW(side) : rotateSideCCW(side);
          const list = placed[side].map((e) => e.port.id);
          if (list.length > 0) nextPorts[nextSide] = list;
        }
        return {
          ...n,
          data: {
            ...n.data,
            rotation: nextRot,
            ports: nextPorts,
          },
        };
      }),
    );
    pickedBlocks.forEach((b) => updateNodeInternals(b.id));
  };

  const flipSelected = (axis: "x" | "y") => {
    if (pickedBlocks.length === 0) return;
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) =>
      ns.map((n) => {
        if (!ids.has(n.id) || !isBlock(n)) return n;
        const currentRot = (n.data.rotation ?? 0) as RotationDeg;
        const currentMirror = Boolean(n.data.mirror);
        const def = inForm(n.data.def, n.data.params);
        const placed = placePorts(def, n.data.ports);
        const nextPorts: PortSides = {};
        for (const side of SIDES) {
          const nextSide = axis === "x" ? flipSideX(side) : flipSideY(side);
          const list = placed[side].map((e) => e.port.id);
          if (list.length > 0) nextPorts[nextSide] = list;
        }
        if (axis === "x") {
          return {
            ...n,
            data: {
              ...n.data,
              mirror: !currentMirror,
              ports: nextPorts,
            },
          };
        } else {
          return {
            ...n,
            data: {
              ...n.data,
              mirror: !currentMirror,
              rotation: (((currentRot + 180) % 360) as RotationDeg),
              ports: nextPorts,
            },
          };
        }
      }),
    );
    pickedBlocks.forEach((b) => updateNodeInternals(b.id));
  };

  const duplicateSelected = (): boolean => {
    const { nodes: ns, edges: es } = latest.current;
    const clip = copySelection(ns, es);
    if (!clip) return false;
    const next = pasteClip(clip, ns, es, 30);
    setNodes(next.nodes);
    setEdges(next.edges);
    return true;
  };

  useCanvasHotkeys({
    hasSelection: many,
    onInsert: () => {
      if (pointer.current) setQuick({ ...pointer.current });
    },
    onRotateCW: () => rotateSelected(true),
    onRotateCCW: () => rotateSelected(false),
    onFlipH: () => flipSelected("x"),
    onFlipV: () => flipSelected("y"),
    onDuplicate: () => void duplicateSelected(),
    onDelete: removePicked,
    onComment: () => toggleComment("out"),
    onBypass: () => toggleComment("through"),
    onEscape: () => deselect(),
  });

  if (controls) {
    controls.current = {
      copy: () => void copySelected(false),
      cut: () => void copySelected(true),
      paste: () => void pasteCopied(),
      remove: removePicked,
      group,
      ungroup: () => pickedFrames.forEach((f) => actions.ungroup(f.id)),
      arrange,
      comment: (mode: "out" | "through" = "out") => toggleComment(mode),
      rotate: (cw = true) => rotateSelected(cw),
      flip: (axis = "x") => flipSelected(axis),
      duplicate: () => void duplicateSelected(),
      can: {
        copy: copySelection(nodes, edges) !== null,
        get paste() {
          return copied.current !== null;
        },
        remove: !(kept === pickedNodes.length && pickedWires.length === 0),
        group: canGroup,
        frame: canFrame,
        story: canStory,
        ungroup: pickedFrames.length > 0,
        arrange: !locked && canArrangeFlow(nodes, level),
        comment: pickedBlocks.length > 0,
        rotate: pickedBlocks.length > 0,
        flip: pickedBlocks.length > 0,
        duplicate: pickedNodes.length > 0,
      },
    };
  }
  useEffect(() => () => {
    if (controls) controls.current = null;
  }, [controls]);
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
  const nodeLookup = useStore((st) => st.nodeLookup);
  const wireRoutes = useMemo(() => {
    const obstacles: Obstacle[] = [];
    for (const n of nodeLookup.values()) {
      obstacles.push({
        id: n.id,
        x: n.internals.positionAbsolute.x !== undefined ? n.internals.positionAbsolute.x : n.position.x,
        y: n.internals.positionAbsolute.y !== undefined ? n.internals.positionAbsolute.y : n.position.y,
        width: n.measured?.width ?? n.width ?? 190,
        height: n.measured?.height ?? n.height ?? 130,
      });
    }

    const routeInputs: RouteInput[] = [];
    for (const w of shown.edges) {
      const sn = nodeLookup.get(w.source);
      const tn = nodeLookup.get(w.target);
      if (!sn || !tn) continue;

      const sHandles = sn.internals.handleBounds?.source ?? [];
      const sh = sHandles.find((h) => (h.id ?? null) === (w.sourceHandle ?? null)) ?? sHandles[0];

      const tHandles = tn.internals.handleBounds?.target ?? [];
      const th = tHandles.find((h) => (h.id ?? null) === (w.targetHandle ?? null)) ?? tHandles[0];

      const sx = sh ? sn.internals.positionAbsolute.x + sh.x + sh.width / 2 : sn.internals.positionAbsolute.x + (sn.measured?.width ?? 190);
      const sy = sh ? sn.internals.positionAbsolute.y + sh.y + sh.height / 2 : sn.internals.positionAbsolute.y + (sn.measured?.height ?? 130) / 2;

      const tx = th ? tn.internals.positionAbsolute.x + th.x + th.width / 2 : tn.internals.positionAbsolute.x;
      const ty = th ? tn.internals.positionAbsolute.y + th.y + th.height / 2 : tn.internals.positionAbsolute.y + (tn.measured?.height ?? 130) / 2;

      const toSide = (pos?: Position | string): "left" | "right" | "top" | "bottom" => {
        if (pos === Position.Left || pos === "left") return "left";
        if (pos === Position.Top || pos === "top") return "top";
        if (pos === Position.Bottom || pos === "bottom") return "bottom";
        return "right";
      };

      routeInputs.push({
        id: w.id,
        source: w.source,
        sourceHandle: w.sourceHandle,
        target: w.target,
        targetHandle: w.targetHandle,
        sx,
        sy,
        tx,
        ty,
        sourcePosition: toSide(sh?.position),
        targetPosition: toSide(th?.position ?? Position.Left),
        step: w.data?.type === "step",
      });
    }
    return routeWires(routeInputs, obstacles);
  }, [shown.edges, nodeLookup]);
  const setBends = useMemo(
    () => (locked ? null : (id: string, bends: WirePoint[]) => setEdges((ws) => ws.map((w) => (w.id === id ? { ...w, data: { type: w.data?.type ?? "signal", ...w.data, points: bends } } : w)))),
    [locked, setEdges],
  );

  const box = wrap.current?.getBoundingClientRect();
  const rel = (x: number, y: number) => ({ x: x - (box?.left ?? 0), y: y - (box?.top ?? 0) });
  const commentedOut = useMemo(
    () => new Set(nodes.filter(isBlock).filter((n) => n.data.commented && n.data.commentMode !== "through").map((n) => n.id)),
    [nodes],
  );

  return (
    <GroupActions.Provider value={{ ...actions, enter: go, stateOf }}>
    <HotWire.Provider value={hint?.plan.kind === "insert" ? hint.plan.wire.id : null}>
    <Lit.Provider value={lit}>
    <WireRoutes.Provider value={wireRoutes}>
    <SetBends.Provider value={setBends}>
    <ConnectorsContext.Provider value={connectors}>
    <IconWires.Provider value={iconWires}>
    <CommentedOutContext.Provider value={commentedOut}>
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
              disabled={pickedBlocks.length === 0}
              title="Rotate 90° clockwise (R)"
              onClick={() => rotateSelected(true)}
            >
              Rotate
            </button>
            <button
              className="key small"
              disabled={pickedBlocks.length === 0}
              title="Flip horizontal (F)"
              onClick={() => flipSelected("x")}
            >
              Flip
            </button>
            <button
              className="key small"
              disabled={pickedBlocks.length === 0}
              title="Flip vertical (Alt+F)"
              onClick={() => flipSelected("y")}
            >
              Flip ⇅
            </button>
            <button
              className="key small"
              disabled={pickedBlocks.length === 0}
              title="Duplicate (Ctrl+D)"
              onClick={() => duplicateSelected()}
            >
              Duplicate
            </button>
            <button
              className="key small"
              disabled={pickedBlocks.length === 0}
              title={allCommented ? "Uncomment selected blocks (Ctrl+/)" : "Comment out selected blocks (Ctrl+/)"}
              onClick={() => toggleComment("out")}
            >
              {allCommented ? "Uncomment" : "Comment"}
            </button>
            <button
              className="key small"
              disabled={pickedBlocks.length === 0}
              title="Bypass (comment through) selected blocks (Ctrl+Shift+Y)"
              onClick={() => toggleComment("through")}
            >
              Bypass
            </button>
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
            {levelGroup && pickedBlock && <ExposeMenu block={pickedBlock} group={levelGroup} set={(expose) => actions.update(levelGroup.id, { expose })} />}
            {kept > 0 && (
              <span className="lbl kept" title="Linked blocks come from the backend's own graph">
                {kept} linked {kept === 1 ? "block stays" : "blocks stay"}
              </span>
            )}
          </NodeToolbar>
        </ReactFlow>
        </WireDragContext.Provider>
        {shown.nodes.length === 0 && (
          <p className="canvas-empty">
            <span>
              <span className="kbd">/</span> or double-click to add a block
            </span>
          </p>
        )}
        <CanvasHelp />
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
    </div>
    </CommentedOutContext.Provider>
    </IconWires.Provider>
    </ConnectorsContext.Provider>
    </SetBends.Provider>
    </WireRoutes.Provider>
    </Lit.Provider>
    </HotWire.Provider>
    </GroupActions.Provider>
  );
}

/** What the canvas does with a pointer and keys, one line each. */
const GESTURES: [string, string][] = [
  ["Add a block", "/ or double-click the canvas"],
  ["Select", "Click a block · Ctrl-drag a box · Ctrl-click adds"],
  ["Rotate", "R turns clockwise · Shift+R counter-clockwise"],
  ["Flip", "F mirrors horizontal · Alt+F vertical"],
  ["Duplicate", "Ctrl+D · Alt-drag a block"],
  ["Act on a selection", "The dialog above it: rotate, flip, duplicate, delete, group, comment"],
  ["Comment out · bypass", "Ctrl+/ · Ctrl+Shift+Y"],
  ["Pan · zoom", "Drag the canvas · the wheel"],
  ["Remove a wire", "Drag its end off the port"],
  ["Move a port", "Drag its name to another side · double-click puts it back"],
  ["Open a group", "Double-click it · Alt+↑ or Esc comes back · ▾ collapses"],
  ["Undo · redo", "Ctrl+Z · Ctrl+Shift+Z"],
];

/** The canvas's "?" key: the gestures as a short list, on demand. Esc or a click elsewhere closes it. */
function CanvasHelp() {
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Escape") return;
      e.stopPropagation(); // not "go up a level" as well
      setOpen(false);
    };
    const onDown = (e: PointerEvent) => !box.current?.contains(e.target as globalThis.Node) && setOpen(false);
    window.addEventListener("keydown", onKey, true);
    window.addEventListener("pointerdown", onDown, true);
    return () => {
      window.removeEventListener("keydown", onKey, true);
      window.removeEventListener("pointerdown", onDown, true);
    };
  }, [open]);
  return (
    <div ref={box} className="nodrag">
      {open && (
        <section className="help-pop" role="dialog" aria-label="Canvas gestures">
          <h3>On the canvas</h3>
          <dl>
            {GESTURES.map(([what, how]) => (
              <Fragment key={what}>
                <dt>{what}</dt>
                <dd>{how}</dd>
              </Fragment>
            ))}
          </dl>
        </section>
      )}
      <button className={`key small help-key${open ? " on" : ""}`} aria-expanded={open} title="What the canvas does" onClick={() => setOpen(!open)}>
        ?
      </button>
    </div>
  );
}

const hasStepPorts = (def: BlockDef) => [...def.inputs, ...def.outputs].some((p) => p.type === "step");

/**
 * A block's ports as checkboxes: a ticked one is a port of the open group (shown on its block
 * from outside) even with no wire yet. Ports with a wire across the group's edge show anyway.
 */
function ExposeMenu({ block, group, set }: { block: BlockNode; group: GroupNode; set: (expose: Expose[]) => void }) {
  const def = portsOf(block);
  const list = group.data.expose ?? [];
  const is = (side: Expose["side"], port: string) => list.some((x) => x.side === side && x.to[0] === block.id && x.to[1] === port);
  const toggle = (side: Expose["side"], port: string) =>
    set(is(side, port) ? list.filter((x) => !(x.side === side && x.to[0] === block.id && x.to[1] === port)) : [...list, { side, to: [block.id, port] }]);
  const rows = [...def.inputs.map((p) => ({ side: "in" as const, id: p.id })), ...def.outputs.map((p) => ({ side: "out" as const, id: p.id }))];
  if (rows.length === 0) return null;
  return (
    <details className="expose-menu">
      <summary className="key small" title="Show a port of this block on the group, even with no wire yet">
        Expose port
      </summary>
      <div className="expose-list">
        {rows.map((r) => (
          <label key={`${r.side}:${r.id}`} className="expose-row">
            <input type="checkbox" checked={is(r.side, r.id)} onChange={() => toggle(r.side, r.id)} />
            <span className="lbl">{r.side === "in" ? "in" : "out"}</span> {r.id}
          </label>
        ))}
      </div>
    </details>
  );
}
