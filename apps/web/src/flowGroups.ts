// Grouping on the Flow canvas: a subsystem, story or frame round the selected blocks and groups of
// the open level, and what the group boxes on it can do (rename, ungroup, show how busy they are).

import { useContext, useMemo, type Dispatch, type SetStateAction } from "react";
import { descendants, frameAround, isFrame, isLevel, ordered, withoutFrame, type GroupData } from "./groups";
import { RunStates } from "./flowContexts";
import { freeId, hasStepPorts, isBlock, portsOf, type FlowNode, type Wire } from "./flowModel";

/** `shownIds`: the nodes on the open level, as the canvas shows them. */
export function useFlowGroups(
  nodes: FlowNode[],
  shownIds: Set<string>,
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
) {
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
  return { actions, stateOf, canGroup, canFrame, canStory, group };
}
