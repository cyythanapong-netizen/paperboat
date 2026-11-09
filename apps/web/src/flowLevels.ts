// Levels (#68): a subsystem or story opens on the Flow canvas; the way back is the breadcrumb.
// Each level keeps its own view, and Alt+↑ (or Escape with nothing selected) goes up one.

import { useReactFlow, type Viewport } from "@xyflow/react";
import { useEffect, useRef, type Dispatch, type SetStateAction } from "react";
import { ownKeys } from "./commands";
import { isLevel, levelPath } from "./groups";
import type { FlowNode, Wire } from "./flowModel";

/** `wanted`: the level the app asks for; the open one is that, if it is still there. */
export function useFlowLevels(
  wanted: string | null,
  setLevel: (level: string | null) => void,
  nodes: FlowNode[],
  edges: Wire[],
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
) {
  // The open level, if it is still there (undo or ungroup may have taken it away).
  const level = wanted && isLevel(nodes.find((n) => n.id === wanted)) ? wanted : null;
  useEffect(() => {
    if (wanted !== level) setLevel(level);
  }, [wanted, level, setLevel]);
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
  return { level, go, path };
}
