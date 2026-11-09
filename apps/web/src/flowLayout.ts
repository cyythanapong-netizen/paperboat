import type { GroupNode } from "./groups";
import type { BlockNode, FlowNode, Wire } from "./flowModel";
import { layoutGraph, type LayoutEdge, type LayoutNode, type LayoutPoint } from "./graphLayout.ts";

const BLOCK_W = 190;
const BLOCK_H = 130;
const FRAME_PAD = 24;
const FRAME_HEAD = 34;

const isFrame = (n: FlowNode | undefined): n is GroupNode => n?.type === "frame";
const isBlock = (n: FlowNode | undefined): n is BlockNode => n?.type === "block";
const positive = (value: number | undefined, fallback: number) => (value !== undefined && Number.isFinite(value) && value > 0 ? value : fallback);
const samePoint = (a: LayoutPoint, b: LayoutPoint) => a.x === b.x && a.y === b.y;

/** A node's current size. An open frame's saved size wins over a stale measurement after a refit. */
function sizeOf(n: FlowNode): { width: number; height: number } {
  const openFrame = isFrame(n) && n.data.kind === "visual" && !n.data.collapsed;
  return {
    width: openFrame ? positive(n.data.size.w, BLOCK_W) : positive(n.measured?.width, positive(n.width, BLOCK_W)),
    height: openFrame ? positive(n.data.size.h, BLOCK_H) : positive(n.measured?.height, positive(n.height, BLOCK_H)),
  };
}

/** True when the open level itself has something the arrange action can move. */
export function canArrangeFlow(nodes: FlowNode[], level: string | null): boolean {
  return nodes.some((n) => (n.parentId ?? null) === level);
}

export type PortOffsetResolver = (
  nodeId: string,
  handleId: string | null | undefined,
  dir: "source" | "target",
) => number | undefined;

/**
 * Arrange `level` and every group below it, innermost first. Open visual frames are units in
 * their parent and layouts of their own inside; subsystems and stories are cards outside and
 * levels inside. Generated In/Out nodes take part in a level's layout but are never written
 * back. Only node positions and visual-frame sizes change; the input is left untouched.
 */
export function arrangeFlowNodes(
  nodes: FlowNode[],
  edges: Wire[],
  level: string | null = null,
  portOffset?: PortOffsetResolver,
): FlowNode[] {
  const originalById = new Map(nodes.map((n) => [n.id, n]));
  const root = level ? originalById.get(level) : undefined;
  if (level && (!isFrame(root) || root.data.kind === "visual")) return nodes;
  const rootGroup = isFrame(root) ? root : undefined;

  const byId = new Map(originalById);
  const parentOf = (n: FlowNode | undefined) => (n?.parentId ? byId.get(n.parentId) : undefined);
  const inScope = (n: FlowNode): boolean => {
    if (level === null) return true;
    for (let at: FlowNode | undefined = n, guard = 0; at && guard <= nodes.length; at = parentOf(at), guard++) {
      if (at.id === level) return true;
    }
    return false;
  };
  const depth = (n: FlowNode): number => {
    let d = 0;
    for (let at = parentOf(n), guard = 0; at && guard <= nodes.length; at = parentOf(at), guard++) d++;
    return d;
  };

  type Container = { id: string | null; node?: GroupNode };
  const containers: Container[] = [
    ...nodes.filter((n): n is GroupNode => isFrame(n) && n.id !== level && inScope(n)).map((node) => ({ id: node.id, node })),
    ...(rootGroup ? [{ id: rootGroup.id, node: rootGroup }] : [{ id: null }]),
  ];
  containers.sort((a, b) => (b.node ? depth(b.node) : -1) - (a.node ? depth(a.node) : -1) || (String(a.id) < String(b.id) ? -1 : String(a.id) > String(b.id) ? 1 : 0));

  let changed = false;
  for (const container of containers) {
    const direct = [...byId.values()].filter((n) => (n.parentId ?? null) === container.id);
    if (direct.length === 0) continue;
    const directIds = new Set(direct.map((n) => n.id));

    // Where an endpoint appears in this container: its direct child, or undefined when it is
    // outside. Walking rather than flattening keeps every parent's children in its own space.
    const project = (id: string): string | undefined => {
      if (id === container.id) return undefined;
      let at = byId.get(id);
      for (let guard = 0; at && guard <= nodes.length; guard++) {
        if ((at.parentId ?? null) === container.id) return directIds.has(at.id) ? at.id : undefined;
        at = parentOf(at);
      }
      return undefined;
    };

    const layoutNodes = new Map<string, LayoutNode>();
    for (const n of direct) layoutNodes.set(n.id, { id: n.id, ...sizeOf(n) });
    const layoutEdges: LayoutEdge[] = [];
    const boundary = (dir: "in" | "out", id: string, port: string | null | undefined): string => {
      const label = `${id}.${port ?? ""}`;
      const key = `${dir}:${label}`;
      const bid = `arrange:${container.id ?? "top"}:${key}`;
      if (!layoutNodes.has(bid)) {
        layoutNodes.set(bid, {
          id: bid,
          width: Math.round(48 + 7 * label.length),
          height: 28,
          pin: dir === "in" ? "first" : "last",
        });
      }
      return bid;
    };

    const resolveOffset = (nodeId: string, handleId: string | null | undefined, dir: "source" | "target"): number | undefined => {
      if (portOffset) {
        const off = portOffset(nodeId, handleId, dir);
        if (off !== undefined) return off;
      }
      const n = byId.get(nodeId);
      if (isBlock(n)) {
        const def = n.data.def;
        const list = dir === "source" ? def.outputs : def.inputs;
        const idx = list.findIndex((p) => p.id === handleId);
        if (idx >= 0) return 34 + (idx + 0.5) * 24;
      }
      return undefined;
    };

    for (const e of edges) {
      // A story's own step wires belong outside it, not to the layout of its contents.
      if (container.id !== null && (e.source === container.id || e.target === container.id)) continue;
      const source = project(e.source);
      const target = project(e.target);
      if (source && target) {
        if (source !== target) {
          layoutEdges.push({
            source,
            target,
            sourceOffset: resolveOffset(e.source, e.sourceHandle, "source"),
            targetOffset: resolveOffset(e.target, e.targetHandle, "target"),
          });
        }
      } else if (target) {
        const bid = boundary("in", e.source, e.sourceHandle);
        layoutEdges.push({
          source: bid,
          target,
          sourceOffset: 14,
          targetOffset: resolveOffset(e.target, e.targetHandle, "target"),
        });
      } else if (source) {
        const bid = boundary("out", e.target, e.targetHandle);
        layoutEdges.push({
          source,
          target: bid,
          sourceOffset: resolveOffset(e.source, e.sourceHandle, "source"),
          targetOffset: 14,
        });
      }
    }

    const isVisual = container.node?.data.kind === "visual";
    const origin = isVisual ? { x: FRAME_PAD, y: FRAME_PAD + FRAME_HEAD } : { x: 0, y: 0 };
    const positions = layoutGraph([...layoutNodes.values()], layoutEdges, { origin });
    for (const n of direct) {
      const at = positions.get(n.id);
      if (!at || samePoint(n.position, at)) continue;
      const moved = { ...n, position: { ...at } };
      byId.set(n.id, moved);
      changed = true;
    }

    // A visual frame wraps its newly arranged children. Its own position is set by its
    // parent's pass later; collapsed groups still have a correctly sized frame when opened.
    if (isVisual && container.id !== null) {
      const frame = byId.get(container.id);
      if (!isFrame(frame)) continue;
      const children = [...byId.values()].filter((n) => n.parentId === frame.id);
      const right = Math.max(0, ...children.map((n) => n.position.x + sizeOf(n).width));
      const bottom = Math.max(0, ...children.map((n) => n.position.y + sizeOf(n).height));
      const size = {
        w: Math.max(200, Math.ceil(right + FRAME_PAD)),
        h: Math.max(120, Math.ceil(bottom + FRAME_PAD)),
      };
      if (frame.data.size.w !== size.w || frame.data.size.h !== size.h) {
        byId.set(frame.id, { ...frame, data: { ...frame.data, size } });
        changed = true;
      }
    }
  }

  return changed ? nodes.map((n) => byId.get(n.id) ?? n) : nodes;
}
