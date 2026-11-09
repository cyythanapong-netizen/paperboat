// Drawing and moving wires on the Flow canvas. While a wire is dragged (#52) the port it would join
// is raised and ports that can't take it dim; a drop that misses, near a port that accepts it, joins
// that one. Dragging a wire's end onto another port moves it; dropping it anywhere else deletes it.

import type { Connection, HandleType, OnConnectEnd, OnConnectStart, ReactFlowState, XYPosition } from "@xyflow/react";
import { useEffect, useRef, useState, type Dispatch, type SetStateAction } from "react";
import { connectionOf, nearestAccepting, SNAP, type Anchor } from "./wireSnap";
import { connect, type Wire } from "./flowModel";
import type { wireRules } from "./wireRules";

export function useWireDrag(
  { real, wireType, isValidConnection, refusal }: ReturnType<typeof wireRules>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
  store: { getState: () => ReactFlowState },
  screenToFlowPosition: (at: XYPosition) => XYPosition,
) {
  const onConnect = (shown: Connection) => {
    const c = real(shown);
    setEdges((ws) => connect(ws, c, wireType(c)));
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

  // What the canvas calls: React Flow's connect and reconnect events.
  const onReconnectStart = (_: unknown, wire: Wire, end: HandleType) => {
    moved.current = false;
    // The end that stays put is the anchor.
    startWire(end === "target" ? { nodeId: wire.source, handleId: wire.sourceHandle ?? null, type: "source" } : { nodeId: wire.target, handleId: wire.targetHandle ?? null, type: "target" });
  };
  const onConnectStart: OnConnectStart = (_, from) => from.nodeId && from.handleType && startWire({ nodeId: from.nodeId, handleId: from.handleId, type: from.handleType });
  const onConnectEnd: OnConnectEnd = (_, state) => {
    // Same: nothing accepting was snapped to, but one is within reach.
    if (!state.isValid && anchor && targetNow.current) onConnect(connectionOf(anchor, targetNow.current.nodeId, targetNow.current.handleId));
    endWire();
  };
  const handlers = { onConnect, onReconnect, onReconnectStart, onReconnectEnd, onConnectStart, onConnectEnd };
  return { wireDrag, handlers };
}
