import {
  Background,
  Handle,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useEdgesState,
  useNodesState,
  useReactFlow,
  type Connection,
  type Edge,
  type EdgeChange,
  type Node,
  type NodeChange,
  type NodeProps,
} from "@xyflow/react";
import { useEffect, useRef } from "react";
import type { ObjectDef, Station, StationObject } from "./protocol";
import { PageIcon } from "./Shell";

type ObjectData = { object: StationObject; def?: ObjectDef };
type ObjectNode = Node<ObjectData, "object">;
type LinkEdge = Edge;

/** A station object as a node: its link slots on the left, itself on the right. */
function ObjectBox({ data }: NodeProps<ObjectNode>) {
  const { object, def } = data;
  const props = Object.entries(object.props ?? {});
  return (
    <div className={`block object station-node role-${def?.role ?? "body"}${def ? "" : " missing"}`}>
      <div className="block-title">
        <span className="block-name" title={def?.block ? "Has a linked block on the Flow page, marked with the Station glyph and the same id" : undefined}>
          {def?.label ?? object.type} <span className="linked-tag">{def?.block && <PageIcon page="station" className="link-mark" />}{object.id}</span>
        </span>
        <span className={`block-tag${def ? "" : " tag-missing"}`}>{def ? (def.role === "joint" ? "joint" : "object") : "missing"}</span>
      </div>
      {(def?.links ?? []).map((slot) => (
        <div key={slot} className="port in port-link">
          <Handle type="target" position={Position.Left} id={slot} />
          {slot}
        </div>
      ))}
      <div className="port out port-link">
        {def?.block ? <><PageIcon page="station" className="link-mark" />block</> : ""}
        <Handle type="source" position={Position.Right} id="self" />
      </div>
      {props.length > 0 && (
        <div className="station-node-props muted">
          {props.map(([k, v]) => `${k} ${typeof v === "number" ? +v.toFixed(3) : v}`).join(" · ")}
        </div>
      )}
    </div>
  );
}

const nodeTypes = { object: ObjectBox };

function toNodes(station: Station, defs: Map<string, ObjectDef>, locked: boolean): ObjectNode[] {
  return station.objects.map((o, i) => ({
    id: o.id,
    type: "object",
    position: { x: o.pos?.[0] ?? 220 * i, y: o.pos?.[1] ?? 0 },
    draggable: !locked,
    data: { object: o, def: defs.get(o.type) },
  }));
}

function toEdges(station: Station): LinkEdge[] {
  return station.objects.flatMap((o) =>
    Object.entries(o.links ?? {}).map(([slot, target]) => ({
      id: `${target}->${o.id}.${slot}`,
      source: target,
      sourceHandle: "self",
      target: o.id,
      targetHandle: slot,
      className: "station-link",
    })),
  );
}

/** Without `id`, and without any link to it. */
export function withoutObject(station: Station, id: string): Station {
  return {
    objects: station.objects
      .filter((o) => o.id !== id)
      .map((o) =>
        Object.values(o.links ?? {}).includes(id)
          ? { ...o, links: Object.fromEntries(Object.entries(o.links ?? {}).filter(([, t]) => t !== id)) }
          : o,
      ),
  };
}

type Props = { objects: ObjectDef[]; station: Station; onChange: (station: Station) => void; locked: boolean };

function Graph({ objects, station, onChange, locked }: Props) {
  const [nodes, setNodes, onNodesChange] = useNodesState<ObjectNode>([]);
  const [edges, setEdges] = useEdgesState<LinkEdge>([]);
  const { fitView } = useReactFlow();
  // Shown after being hidden (another page in Navigator, or another mode): it was laid out
  // at no size, so fit it again once it has one.
  const box = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = box.current;
    if (!el) return;
    let wasEmpty = el.clientWidth === 0;
    const watch = new ResizeObserver(() => {
      const empty = el.clientWidth === 0;
      if (wasEmpty && !empty) requestAnimationFrame(() => fitView());
      wasEmpty = empty;
    });
    watch.observe(el);
    return () => watch.disconnect();
  }, [fitView]);

  // The station is the truth; the graph follows it (positions included).
  useEffect(() => {
    const byType = new Map(objects.map((o) => [o.type, o]));
    setNodes((old) => {
      const selected = new Set(old.filter((n) => n.selected).map((n) => n.id));
      return toNodes(station, byType, locked).map((n) => ({ ...n, selected: selected.has(n.id) }));
    });
    setEdges(toEdges(station));
  }, [station, objects, locked, setNodes, setEdges]);

  const update = (id: string, change: (o: StationObject) => StationObject) =>
    onChange({ objects: station.objects.map((o) => (o.id === id ? change(o) : o)) });

  // Linking: drag from an object's right handle onto another's slot. A slot holds one link.
  const onConnect = (c: Connection) => {
    if (locked || !c.targetHandle || c.source === c.target) return;
    update(c.target, (o) => ({ ...o, links: { ...o.links, [c.targetHandle!]: c.source } }));
  };

  const onEdgesChange = (changes: EdgeChange<LinkEdge>[]) => {
    const gone = changes.flatMap((c) => (c.type === "remove" ? [edges.find((e) => e.id === c.id)] : [])).filter(Boolean);
    if (gone.length === 0 || locked) {
      setEdges((es) => es.map((e) => changes.reduce((acc, c) => (c.type === "select" && c.id === acc.id ? { ...acc, selected: c.selected } : acc), e)));
      return;
    }
    let next = station;
    for (const e of gone) {
      next = {
        objects: next.objects.map((o) =>
          o.id === e!.target ? { ...o, links: Object.fromEntries(Object.entries(o.links ?? {}).filter(([slot]) => slot !== e!.targetHandle)) } : o,
        ),
      };
    }
    onChange(next);
  };

  const onNodesChangeHere = (changes: NodeChange<ObjectNode>[]) => {
    const removed = changes.flatMap((c) => (c.type === "remove" ? [c.id] : []));
    if (removed.length > 0) {
      if (!locked) onChange(removed.reduce(withoutObject, station));
      return;
    }
    onNodesChange(changes);
  };

  // A moved object keeps its place: written to the station when the drag ends.
  const onNodeDragStop = (_: unknown, node: ObjectNode) =>
    update(node.id, (o) => ({ ...o, pos: [Math.round(node.position.x), Math.round(node.position.y)] }));

  return (
    <div className="station-graph" ref={box}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChangeHere}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
        onNodeDragStop={onNodeDragStop}
        nodesConnectable={!locked}
        deleteKeyCode={locked ? null : ["Backspace", "Delete"]}
        panActivationKeyCode={null}
        fitView
      >
        <Background />
      </ReactFlow>
    </div>
  );
}

/**
 * The Station as a graph (`[station] view = "graph"`): each station object is a node, and
 * its links to other objects (a spring's two ends) are wires into its slots. Build the
 * structure here; set properties on the Station panel; the Flow page holds the logic.
 */
export function StationGraph(props: Props) {
  // Its own React Flow store: the Flow page has the other one.
  return (
    <ReactFlowProvider>
      <Graph {...props} />
    </ReactFlowProvider>
  );
}
