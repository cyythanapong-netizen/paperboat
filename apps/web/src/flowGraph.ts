// The Flow canvas and the wire graph the engine runs (and a project file keeps): from one to the
// other, groups included, and new blocks placed on the level that is open.

import { absolute, frameAround, isFrame, ordered, STORY_DEF, toGroups } from "./groups";
import type { BlockDef, FlowType, Graph } from "./protocol";
import { freeId, inForm, isBlock, makeNode, makeWire, missingDef, portType, type FlowNode, type Wire } from "./flowModel.ts";

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

/** The canvas as the wire graph; `flow`: the sheet's flow type, if one was chosen (#9). */
export function toGraph(nodes: FlowNode[], edges: Wire[], flow?: FlowType | null): Graph {
  const groups = toGroups(nodes);
  return {
    ...(flow ? { flow } : {}),
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
export function toFlow(nodes: FlowNode[], edges: Wire[], flow?: FlowType | null): Graph {
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const g = toGraph(nodes, edges, flow);
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
export function inLevel<N extends FlowNode>(node: N, level: string | null, nodes: FlowNode[]): N {
  const host = level ? nodes.find((n) => n.id === level) : undefined;
  if (!host) return node;
  const o = absolute(host, nodes);
  return { ...node, parentId: level, position: { x: node.position.x - o.x, y: node.position.y - o.y } };
}
