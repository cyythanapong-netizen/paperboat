// Toolkit analyses (pb#370): what a toolkit computes about a part of the graph on request,
// outside any run and any flow. The app owns the entry points (a subsystem's "analyse", the
// Analyse commands on a selection) and the drawer that asks and shows; a toolkit's web part
// registers the panel that draws its result, and its engine side registers the computation.
// An analysis is offered only when both are there: the backend's catalog lists it and a
// toolkit's web part registered its panel.
import type { ComponentType } from "react";
import type { FlowNode, Wire } from "./flowModel";
import type { AnalysisDef, ClientMsg, Graph } from "./protocol";

/** What is being analysed: a subsystem or a selection, as the blocks in it and its boundary. */
export interface AnalysisTarget {
  /** What the drawer calls it: the subsystem's label, or "3 blocks". */
  label: string;
  /** The blocks in it (a group's, all the way down), monitors left out. */
  ids: string[];
  /** u: its inputs not fed from inside it, as [node, port]. */
  inputs: [string, string][];
  /** y: its outputs read outside it (or by a monitor), or not read at all. */
  outputs: [string, string][];
}

/** What the drawer hands an analysis's panel. */
export interface AnalysisPanelProps {
  analysis: AnalysisDef;
  target: AnalysisTarget;
  /** The analysis's last answer (its own shape); undefined until the first. */
  result: unknown;
  /** Why the last request has no result. */
  error?: string;
  /** A request is out. */
  busy: boolean;
  /** The panel's own settings, sent with each request (which input and output, say). */
  options: Record<string, unknown>;
  setOptions: (next: Record<string, unknown>) => void;
}
export type AnalysisPanel = ComponentType<AnalysisPanelProps>;

const panels = new Map<string, AnalysisPanel>();

/** Show the result of the analysis `name` ("control/linear") with this panel; a later call replaces it. */
export function registerAnalysis(name: string, panel: AnalysisPanel): void {
  panels.set(name, panel);
}

export function analysisPanel(name: string): AnalysisPanel | undefined {
  return panels.get(name);
}

/** The catalog's analyses that a toolkit's web part can show: what the entry points offer. */
export function offeredAnalyses(defs: readonly AnalysisDef[] | undefined): AnalysisDef[] {
  return (defs ?? []).filter((d) => panels.has(d.name));
}

const isMonitor = (n: FlowNode | undefined) => n?.type === "block" && n.data.def.kind === "monitor";

/**
 * The part of the graph made of `picked` (blocks, and groups standing for everything inside
 * them): its blocks and its boundary. An input is u when nothing in the part feeds it; an
 * output is y when something outside the part (or a monitor) reads it, or nothing reads it.
 */
export function analysisTarget(nodes: FlowNode[], edges: Wire[], picked: string[], label: string): AnalysisTarget {
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const kids = new Map<string, string[]>();
  for (const n of nodes) if (n.parentId) kids.set(n.parentId, [...(kids.get(n.parentId) ?? []), n.id]);
  const ids = new Set<string>();
  const add = (id: string) => {
    const n = byId.get(id);
    if (n?.type === "block" && !isMonitor(n)) ids.add(id);
    for (const k of kids.get(id) ?? []) add(k);
  };
  picked.forEach(add);
  const inside = (id: string) => ids.has(id);
  const signal = (type?: string) => type !== "step";
  const inputs: [string, string][] = [];
  const outputs: [string, string][] = [];
  for (const id of ids) {
    const def = (byId.get(id) as Extract<FlowNode, { type: "block" }>).data.def;
    for (const p of def.inputs)
      if (signal(p.type) && !edges.some((e) => e.target === id && e.targetHandle === p.id && inside(e.source))) inputs.push([id, p.id]);
    for (const p of def.outputs) {
      if (!signal(p.type)) continue;
      const readers = edges.filter((e) => e.source === id && e.sourceHandle === p.id);
      if (readers.length === 0 || readers.some((e) => !inside(e.target))) outputs.push([id, p.id]);
    }
  }
  return { label, ids: [...ids], inputs, outputs };
}

/** The request for `target`: its blocks and the wires between them, from the canvas's graph. */
export function analysisRequest(id: string, analysis: string, graph: Graph, target: AnalysisTarget, options: Record<string, unknown>): Extract<ClientMsg, { type: "analysis.request" }> {
  const ids = new Set(target.ids);
  return {
    type: "analysis.request",
    id,
    analysis,
    graph: {
      nodes: graph.nodes.filter((n) => ids.has(n.id)),
      edges: graph.edges.filter((e) => ids.has(e.from[0]) && ids.has(e.to[0])),
    },
    inputs: target.inputs,
    outputs: target.outputs,
    ...(Object.keys(options).length ? { options } : {}),
  };
}
