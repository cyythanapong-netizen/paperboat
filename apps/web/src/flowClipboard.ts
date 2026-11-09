// Copy, cut and paste on the Flow canvas: Ctrl+C / X / V (⌘ on a Mac) on the Flow page, except while
// typing, and the same actions for the Edit menu; duplicate is a copy pasted at once.

import type { ReactFlowState } from "@xyflow/react";
import { useEffect, useRef, type Dispatch, type MutableRefObject, type SetStateAction } from "react";
import { copySelection, pasteClip, type Clip } from "./clipboard";
import type { FlowNode, Wire } from "./flowModel";

/** `latest`: the canvas as it is now, read when a key is pressed or a menu runs. */
export function useFlowClipboard(
  store: { getState: () => ReactFlowState },
  latest: MutableRefObject<{ nodes: FlowNode[]; edges: Wire[] }>,
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
) {
  const copied = useRef<{ clip: Clip; pastes: number } | null>(null);
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

  const duplicateSelected = (): boolean => {
    const { nodes: ns, edges: es } = latest.current;
    const clip = copySelection(ns, es);
    if (!clip) return false;
    const next = pasteClip(clip, ns, es, 30);
    setNodes(next.nodes);
    setEdges(next.edges);
    return true;
  };
  return { copied, copySelected, pasteCopied, duplicateSelected };
}
