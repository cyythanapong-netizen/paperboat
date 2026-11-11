// Flow types (#9): what a sheet on the Flow page is (a Signal flow, a Step flow…), which ones the
// backend offers, and which blocks the palette offers on the sheet that is open. A block works in
// the flow types its `flows` names; without them (an older catalog, or base and math), in every one.

import type { BlockDef, FlowType } from "./protocol.ts";
import { hasStepPorts, inForm, isBlock, type FlowNode } from "./flowModel.ts";
import type { GroupNode } from "./groups";

const isStory = (n: FlowNode): n is GroupNode => n.type === "frame" && (n as GroupNode).data.kind === "story";

export const FLOW_TYPES: readonly FlowType[] = ["signal", "step", "state", "model"];

/** "Signal flow", "Step flow"… */
export const flowLabel = (flow: FlowType) => `${flow[0].toUpperCase()}${flow.slice(1)} flow`;

/** A block's flow types in words: "Step flow", "Signal flow or Step flow". */
export function flowsLabel(flows: readonly FlowType[]): string {
  const words = flows.map(flowLabel);
  return words.length > 1 ? `${words.slice(0, -1).join(", ")} or ${words.at(-1)}` : (words[0] ?? "every flow type");
}

/** Whether a block works on a sheet of type `flow`: one with no `flows` works in every one.
 *  `mixed`: a signal/step sheet admits the other one too (step paths on a Signal flow, signal
 *  helpers on a Step flow); a story passes false. */
export const worksIn = (def: BlockDef, flow: FlowType, mixed = false) => {
  if (!def.flows?.length || def.flows.includes(flow)) return true;
  return mixed && (flow === "signal" || flow === "step") && def.flows.some((f) => f === "signal" || f === "step");
};

/**
 * The flow types a sheet may be: what the backend's hello says. An older backend doesn't say:
 * a Signal flow, and a Step flow if a block of its catalog has step ports.
 */
export function offeredFlows(hello: readonly FlowType[] | undefined, blocks: BlockDef[]): FlowType[] {
  if (hello?.length) return FLOW_TYPES.filter((f) => hello.includes(f));
  return blocks.some((b) => hasStepPorts(b) || b.forms?.some((f) => [...f.inputs, ...f.outputs].some((p) => p.type === "step")))
    ? ["signal", "step"]
    : ["signal"];
}

/** A top-level sheet whose type wasn't chosen: a Step flow if a block on it has step ports or a story is on it, else a Signal flow. */
export function inferFlow(nodes: FlowNode[]): FlowType {
  return hasStepLayer(nodes) ? "step" : "signal";
}

/** A top-level block with step ports, or a story, is the step layer. */
export function hasStepLayer(nodes: FlowNode[]): boolean {
  // Only the sheet's own level: a step block inside a subsystem is that subsystem's business.
  const top = nodes.filter((n) => !n.parentId);
  return top.some((n) => (isBlock(n) ? !n.data.missing && hasStepPorts(inForm(n.data.def, n.data.params)) : isStory(n)));
}

/** A top-level signal block is the always-on layer, beside any step paths. */
export function hasSignalLayer(nodes: FlowNode[]): boolean {
  return nodes.some((n) => !n.parentId && isBlock(n) && (n.data.missing || !hasStepPorts(inForm(n.data.def, n.data.params))));
}

/** A signal sheet with step paths, or a step sheet with signal helpers: label it as mixed. */
export const hasMixedLayers = (nodes: FlowNode[]) => hasStepLayer(nodes) && hasSignalLayer(nodes);

/**
 * The sheet open at `level` (a subsystem or story; null: the top level). A story runs its blocks
 * as a Signal flow, so its inside (and a subsystem in it) is one, whatever the top level is; a
 * subsystem is the same flow type as the sheet it is on. `story`: the story it is in, if any.
 */
export function sheetFlow(top: FlowType, level: string | null, nodes: FlowNode[]): { flow: FlowType; story?: string } {
  const byId = new Map(nodes.map((n) => [n.id, n]));
  for (let id = level, depth = 0; id && depth < 64; depth++) {
    const n = byId.get(id);
    if (!n) break;
    if (isStory(n)) return { flow: "signal", story: n.id };
    id = n.parentId ?? null;
  }
  return { flow: top };
}

/** Palette items for a sheet: those that work in it, and the rest (shown apart, saying where they work). */
export function byFlow<T extends { kind: string; def: object }>(items: T[], flow: FlowType, mixed = false): { fits: T[]; elsewhere: T[] } {
  const fits: T[] = [];
  const elsewhere: T[] = [];
  for (const it of items) (it.kind !== "block" || worksIn(it.def as BlockDef, flow, mixed) ? fits : elsewhere).push(it);
  return { fits, elsewhere };
}

/** Why a block isn't offered on a sheet of type `flow`. */
export const notHere = (def: BlockDef, flow: FlowType) => `Works only in a ${flowsLabel(def.flows ?? [])}, not in this ${flowLabel(flow)}`;
