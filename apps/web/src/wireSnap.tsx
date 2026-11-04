import { Handle, Position, useNodeId, type Connection, type ReactFlowState } from "@xyflow/react";
import { createContext, useContext } from "react";

/**
 * How easily a wire finds a port (#52), in screen px; one place to tune it (#33).
 * `connect`: a drop this close to a port that accepts the wire snaps to it; `reconnect`:
 * how close to a wire's end you grab it to move it.
 */
export const SNAP = { connect: 40, reconnect: 20 };

/** One end of a wire being dragged: the port that stays put. */
export type Anchor = { nodeId: string; handleId: string | null; type: "source" | "target" };

/** What a PortHandle needs while a wire is dragged; null when none is. */
export type WireDrag = {
  /** The port the wire would join if dropped now: the nearest that accepts it. */
  target: { nodeId: string; handleId: string | null } | null;
  /** May this port take the wire? Ports that can't are dimmed. */
  accepts: (nodeId: string, handleId: string | null, side: "source" | "target") => boolean;
};
export const WireDragContext = createContext<WireDrag | null>(null);

/** The connection a wire from `anchor` to the port (`nodeId`, `handleId`) would make. */
export function connectionOf(anchor: Anchor, nodeId: string, handleId: string | null): Connection {
  return anchor.type === "source"
    ? { source: anchor.nodeId, sourceHandle: anchor.handleId, target: nodeId, targetHandle: handleId }
    : { source: nodeId, sourceHandle: handleId, target: anchor.nodeId, targetHandle: anchor.handleId };
}

/**
 * The port nearest `point` (flow coordinates) within `radius` px of it that accepts a wire
 * from `anchor`. React Flow's own snap takes the nearest port whether or not it accepts,
 * so a wrong-type port beside the right one would steal the drop.
 */
export function nearestAccepting(
  state: ReactFlowState,
  point: { x: number; y: number },
  radius: number,
  anchor: Anchor,
  valid: (c: Connection) => boolean,
): { nodeId: string; handleId: string | null } | null {
  const reach = radius / state.transform[2]; // px on screen to flow units
  const side = anchor.type === "source" ? "target" : "source";
  let best: { nodeId: string; handleId: string | null } | null = null;
  let bestDistance = reach;
  for (const node of state.nodeLookup.values()) {
    for (const h of node.internals.handleBounds?.[side] ?? []) {
      const d = Math.hypot(
        node.internals.positionAbsolute.x + h.x + h.width / 2 - point.x,
        node.internals.positionAbsolute.y + h.y + h.height / 2 - point.y,
      );
      if (d <= bestDistance && valid(connectionOf(anchor, node.id, h.id ?? null))) {
        best = { nodeId: node.id, handleId: h.id ?? null };
        bestDistance = d;
      }
    }
  }
  return best;
}

/**
 * A port's handle. While a wire is dragged: the port it would join is raised and ringed,
 * ports that can't take it are dimmed. The hit area is a 24px square round the 8px dot.
 */
export function PortHandle({ type, position, id, className }: { type: "source" | "target"; position: Position; id: string; className?: string }) {
  const nodeId = useNodeId();
  const drag = useContext(WireDragContext);
  const state = !drag || !nodeId ? "" : drag.target?.nodeId === nodeId && drag.target.handleId === id ? "wire-target" : drag.accepts(nodeId, id, type) ? "" : "wire-dim";
  return <Handle type={type} position={position} id={id} className={[className, state].filter(Boolean).join(" ")} />;
}
