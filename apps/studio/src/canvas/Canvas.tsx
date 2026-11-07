// The acausal diagram (contracts §8.2): parts on @xyflow/react with the shared Compact plate, icon
// and port rendering. Acausal wires have no direction, causal ones run output to input with an
// arrow; a wire the rules refuse says why in words and is never drawn. Every edit goes to the store.

import {
  Background,
  BaseEdge,
  ConnectionMode,
  getSmoothStepPath,
  MarkerType,
  ReactFlow,
  applyNodeChanges,
  useReactFlow,
  type Edge,
  type EdgeProps,
  type Node,
  type NodeChange,
  type NodeProps,
} from "@xyflow/react";
import { netWireType, roundedPolyline, type BlockDef, type Connectors, type IconDef } from "@paperboat/shared";
import { CompactBlock, SNAP, WireDragContext, type WireDrag } from "@paperboat/shared/components";
import { useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from "react";
import { DRAG_TYPE } from "../library/Library.tsx";
import type { ComponentInst, Connection, StudioApi } from "../model/types.ts";
import { snap } from "../store/exprs.ts";
import type { Actions } from "../store/index.tsx";
import { endOf, pathOf, split, wireError } from "../store/wires.ts";
import { centreFor, placedIcon, squareUp, topLeft } from "./geometry.ts";
import { QuickAdd } from "./QuickAdd.tsx";

type PartData = { part: ComponentInst; icon: IconDef; connectors: Connectors; wires: Record<string, number> };
type PartNode = Node<PartData, "part">;
type WireData = { points?: [number, number][]; kind: string; causal: boolean; missing: boolean };
type WireEdge = Edge<WireData, "wire">;

const noop = () => undefined;

function PartView({ id, data, selected }: NodeProps<PartNode>) {
  const { part, icon, connectors, wires } = data;
  const def = useMemo<BlockDef>(() => ({ type: part.class, label: part.name, kind: "function", inputs: [], outputs: [], params: [], view: { compact: true, icon } }), [part.class, part.name, icon]);
  return <CompactBlock id={id} def={def} params={part.params} selected={selected} onExpand={noop} popover={null} connectors={connectors} wires={wires} />;
}

function WireView({ id, data, selected, markerEnd, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition }: EdgeProps<WireEdge>) {
  const bends = data?.points;
  const path = bends ? roundedPolyline(squareUp([sourceX, sourceY], bends, [targetX, targetY]), 8) : getSmoothStepPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, borderRadius: 8 })[0];
  return (
    <>
      <path d={path} className="wire-casing" />
      <BaseEdge id={id} path={path} markerEnd={markerEnd} interactionWidth={16} className={`wire-${data?.kind ?? "signal"}${data?.missing ? " wire-missing" : ""}${selected ? " lit" : ""}`} />
    </>
  );
}

const nodeTypes = { part: PartView };
const edgeTypes = { wire: WireView };

type Props = { api: StudioApi; actions: Actions; classes: string[]; onOpen(name: string): void };

export function Canvas({ api, actions, classes, onOpen }: Props) {
  const { model, selection } = api;
  const flow = useReactFlow();
  const root = useRef<HTMLDivElement>(null);
  const [wire, setWire] = useState<Connection | null>(null);
  const [quick, setQuick] = useState(false);
  const [anchor, setAnchor] = useState<string | null>(null);

  // --- what the model draws ---
  const counts = useMemo(() => {
    const out: Record<string, Record<string, number>> = {};
    for (const c of model.connections)
      for (const path of [c.a, c.b]) {
        const { part, port } = split(path);
        (out[part] ??= {})[port] = (out[part][port] ?? 0) + 1;
      }
    return out;
  }, [model.connections]);

  // fit the view once, to a model that opens with parts; after that the view is the user's
  useEffect(() => {
    const t = window.setTimeout(() => model.components.length && flow.fitView({ padding: 0.25, maxZoom: 1 }), 200);
    return () => window.clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [model.name]);

  const [nodes, setNodes] = useState<PartNode[]>([]);
  useEffect(() => {
    setNodes(
      model.components.map((part) => {
        const [x, y] = topLeft(part);
        return { id: part.name, type: "part", position: { x, y }, selected: selection.includes(part.name), data: { part, icon: placedIcon(part), connectors: model.connectors, wires: counts[part.name] ?? {} } };
      }),
    );
  }, [model, counts, selection]);

  const edges = useMemo<WireEdge[]>(() => {
    const out: WireEdge[] = [];
    for (const c of model.connections) {
      let [a, b] = [c.a, c.b];
      const ea = endOf(model, a);
      const eb = endOf(model, b);
      if (ea?.kind === "input" && eb?.kind === "output") [a, b] = [b, a]; // a causal wire is drawn from its output
      const pa = split(a);
      const pb = split(b);
      const kind = ea?.kind === "acausal" ? netWireType(model.connectors[ea.cls]) : "signal";
      out.push({
        id: `${c.a}|${c.b}`,
        type: "wire",
        source: pa.part,
        sourceHandle: pa.port,
        target: pb.part,
        targetHandle: pb.port,
        selected: wire?.a === c.a && wire.b === c.b,
        markerEnd: kind === "signal" ? { type: MarkerType.ArrowClosed, width: 14, height: 14 } : undefined,
        data: { ...(c.points ? { points: c.points } : {}), kind, causal: kind === "signal", missing: !ea || !eb },
      });
    }
    return out;
  }, [model, wire]);

  // --- interaction ---
  const onNodesChange = useCallback((changes: NodeChange<PartNode>[]) => setNodes((n) => applyNodeChanges(changes, n)), []);
  const centre = () => {
    const rect = root.current?.getBoundingClientRect();
    const p = flow.screenToFlowPosition({ x: (rect?.left ?? 0) + (rect?.width ?? 0) / 2, y: (rect?.top ?? 0) + (rect?.height ?? 0) / 2 });
    return { x: snap(p.x), y: snap(p.y), rotation: 0 as const };
  };
  const place = (cls: string, at = centre()) => void actions.place(cls, at).then((name) => api.select([name]));

  const onDrop = (e: DragEvent) => {
    const cls = e.dataTransfer.getData(DRAG_TYPE);
    if (!cls) return;
    e.preventDefault();
    const p = flow.screenToFlowPosition({ x: e.clientX, y: e.clientY });
    place(cls, { x: snap(p.x), y: snap(p.y), rotation: 0 });
  };

  const pathFor = (nodeId: string, handle: string | null) => {
    const part = model.components.find((c) => c.name === nodeId);
    return part ? pathOf(part, handle ?? "") : `${nodeId}.${handle ?? ""}`;
  };
  const drag = useMemo<WireDrag | null>(
    () =>
      anchor === null
        ? null
        : {
            target: null,
            accepts: (nodeId, handle) => wireError(model, anchor, pathFor(nodeId, handle)) === null,
            why: (nodeId, handle) => wireError(model, anchor, pathFor(nodeId, handle)),
          },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [anchor, model],
  );

  // `/` and Delete work wherever the focus is, except in a field: the canvas is the whole page
  const keys = useRef((_: KeyboardEvent) => {});
  keys.current = (e) => {
    const t = e.target as HTMLElement | null;
    if (t?.closest("input, textarea, select, [contenteditable], [data-own-keys]") || e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key === "/" && !quick) (e.preventDefault(), setQuick(true));
    else if ((e.key === "Delete" || e.key === "Backspace") && (wire || selection.length)) {
      e.preventDefault();
      void actions.remove(selection.filter((s) => model.components.some((c) => c.name === s)), wire ? [wire] : []);
      setWire(null);
    }
  };
  useEffect(() => {
    const on = (e: KeyboardEvent) => keys.current(e);
    window.addEventListener("keydown", on);
    return () => window.removeEventListener("keydown", on);
  }, []);

  const selectedWire = wire ? `${wire.a} - ${wire.b}` : undefined;
  const plan = useCallback((cls: string) => actions.planSplice(wire!, cls), [actions, wire]);

  return (
    <div ref={root} className="canvas" tabIndex={0} onDragOver={(e) => e.dataTransfer.types.includes(DRAG_TYPE) && e.preventDefault()} onDrop={onDrop} data-testid="canvas">
      <WireDragContext.Provider value={drag}>
        <ReactFlow<PartNode, WireEdge>
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          edgeTypes={edgeTypes}
          onNodesChange={onNodesChange}
          connectionMode={ConnectionMode.Loose}
          connectionRadius={SNAP.connect}
          deleteKeyCode={null}
          zoomOnDoubleClick={false}
          nodesConnectable
          proOptions={{ hideAttribution: true }}
          onConnectStart={(_, from) => from.nodeId && setAnchor(pathFor(from.nodeId, from.handleId))}
          onConnectEnd={() => setAnchor(null)}
          onConnect={(c) => {
            const a = pathFor(c.source, c.sourceHandle);
            const b = pathFor(c.target, c.targetHandle);
            void api.connect(a, b).then((why) => actions.say(why ?? ""));
          }}
          onNodeClick={(e, n) => {
            // the store is the one source of what is selected; a click says what changes
            setWire(null);
            api.select(e.ctrlKey || e.metaKey ? (selection.includes(n.id) ? selection.filter((s) => s !== n.id) : [...selection, n.id]) : [n.id]);
            onOpen(n.id);
          }}
          onEdgeClick={(_, e) => {
            const c = model.connections.find((w) => `${w.a}|${w.b}` === e.id);
            setWire(c ?? null);
            api.select([]);
          }}
          onPaneClick={() => {
            setWire(null);
            api.select([]);
            actions.say("");
          }}
          onNodeDragStop={(_, n) => {
            const part = model.components.find((c) => c.name === n.id);
            if (!part) return;
            const [x, y] = centreFor(part, n.position);
            void actions.move(part.name, { ...part.placement, x: snap(x), y: snap(y) });
          }}
        >
          <Background />
        </ReactFlow>
      </WireDragContext.Provider>
      {actions.notice && (
        <p className="notice" role="status">
          {actions.notice}
        </p>
      )}
      {quick && <QuickAdd names={classes} wire={selectedWire} plan={plan} onPlace={(cls) => place(cls)} onClose={() => (setQuick(false), root.current?.focus())} />}
    </div>
  );
}

