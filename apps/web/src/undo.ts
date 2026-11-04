import type { Edge, Node } from "@xyflow/react";
import { useCallback, useEffect, useRef, useState, type Dispatch, type SetStateAction } from "react";

/** A canvas without what React Flow keeps for itself (selection, sizes, dragging). */
type Snapshot<N, E> = { key: string; nodes: N[]; edges: E[] };

const SETTLE_MS = 300; // changes this close together (a drag) are one step
const DEPTH = 100;

function strip<N extends Node, E extends Edge>(nodes: N[], edges: E[]): Snapshot<N, E> {
  const n = nodes.map(({ selected: _s, dragging: _d, measured: _m, ...rest }) => rest as N);
  const e = edges.map(({ selected: _s, ...rest }) => rest as E);
  return { key: JSON.stringify([n, e]), nodes: n, edges: e };
}

/**
 * Undo and redo for a canvas: Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y (⌘ on a Mac), except while
 * typing in a field, which keeps its own. A step is the canvas once it settles: adding or
 * removing blocks and wires, moving, editing params, grouping. Selecting is not a step.
 * `reset` changing (the backend loaded another graph) clears the history.
 * Returns the same two steps for the Edit menu, and whether each has anything to do.
 */
export function useUndo<N extends Node, E extends Edge>(
  nodes: N[],
  edges: E[],
  setNodes: Dispatch<SetStateAction<N[]>>,
  setEdges: Dispatch<SetStateAction<E[]>>,
  reset: unknown,
): { undo: () => void; redo: () => void; canUndo: boolean; canRedo: boolean } {
  const past = useRef<Snapshot<N, E>[]>([]);
  const future = useRef<Snapshot<N, E>[]>([]);
  const settled = useRef<Snapshot<N, E> | null>(null);
  const restoring = useRef(false);
  const now = strip(nodes, edges);
  const current = useRef(now);
  current.current = now;
  const [depth, setDepth] = useState({ past: 0, future: 0 });
  const sync = useCallback(() => {
    const next = { past: past.current.length, future: future.current.length };
    setDepth((d) => (d.past === next.past && d.future === next.future ? d : next));
  }, []);

  useEffect(() => {
    past.current = [];
    future.current = [];
    settled.current = null;
    sync();
  }, [reset, sync]);

  useEffect(() => {
    if (restoring.current) {
      restoring.current = false;
      settled.current = current.current;
      return;
    }
    const timer = window.setTimeout(() => {
      const last = settled.current;
      if (last && last.key !== current.current.key) {
        past.current = [...past.current.slice(-(DEPTH - 1)), last];
        future.current = [];
      }
      settled.current = current.current;
      sync();
    }, SETTLE_MS);
    return () => window.clearTimeout(timer);
  }, [now.key, sync]);

  const step = useCallback(
    (redo: boolean) => {
      const restore = (to: Snapshot<N, E>) => {
        restoring.current = true;
        setNodes(to.nodes);
        setEdges(to.edges);
      };
      const here = current.current;
      const pending = settled.current && settled.current.key !== here.key ? settled.current : null;
      if (!redo && pending) {
        // A change that hasn't settled yet: undo it first.
        future.current.push(here);
        restore(pending);
      } else if (redo) {
        const next = future.current.pop();
        if (!next) return;
        past.current.push(here);
        restore(next);
      } else {
        const prev = past.current.pop();
        if (!prev) return;
        future.current.push(here);
        restore(prev);
      }
      sync();
    },
    [setNodes, setEdges, sync],
  );

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey)) return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable)) return;
      const key = e.key.toLowerCase();
      const redo = (key === "z" && e.shiftKey) || key === "y";
      if (key !== "z" && !redo) return;
      e.preventDefault();
      step(redo);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [step]);

  return { undo: () => step(false), redo: () => step(true), canUndo: depth.past > 0, canRedo: depth.future > 0 };
}
