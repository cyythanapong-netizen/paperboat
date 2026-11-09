// The Board's side of the Flow: its instruments are the monitor blocks (where they sit, what is wired
// into them), and an instrument's + tap wires a block output into one.

import { useMemo, type Dispatch, type SetStateAction } from "react";
import { absolute } from "./groups";
import { instrumentMeta } from "./instruments";
import { isBlock, makeWire, portsOf, type FlowNode, type Wire } from "./flowModel";

export function useBoardWiring(
  nodes: FlowNode[],
  edges: Wire[],
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
  log: (level: "info" | "warn" | "error", text: string) => void,
) {
  const monitors = useMemo(
    () =>
      Object.fromEntries(
        nodes
          .filter(isBlock)
          .filter((n) => n.data.def.kind === "monitor" || !!n.data.def.instrument)
          .map((n) => {
            const kind = n.data.def.instrument ?? "graph";
            return [n.id, { kind, params: n.data.params, ...instrumentMeta(kind), ...absolute(n, nodes), ...wiring(n) }];
          }),
      ),
    [nodes, edges], // eslint-disable-line react-hooks/exhaustive-deps -- wiring reads nodes and edges
  );
  // A monitor's inputs and the output wired into each, for an instrument's legend.
  function wiring(n: FlowNode) {
    const sources: Record<string, { label: string; unit?: string }> = {};
    for (const e of edges) {
      if (e.target !== n.id || !e.targetHandle) continue;
      const src = nodes.find((m) => m.id === e.source);
      const port = src && isBlock(src) ? portsOf(src).outputs.find((p) => p.id === e.sourceHandle) : undefined;
      sources[e.targetHandle] = { label: `${e.source}.${e.sourceHandle}`, ...(port?.unit ? { unit: port.unit } : {}) };
    }
    return { inputs: isBlock(n) ? portsOf(n).inputs.map((p) => p.id) : [], sources };
  }
  // What an instrument's + tap offers: every block output that carries numbers.
  const taps = useMemo(
    () =>
      nodes.filter(isBlock).flatMap((n) =>
        portsOf(n)
          .outputs.filter((p) => (p.type ?? "signal") !== "step" && p.type !== "text")
          .map((p) => ({ id: `${n.id}\u0000${p.id}`, label: `${n.id}.${p.id}`, ...(p.unit ? { unit: p.unit } : {}) })),
      ),
    [nodes],
  );
  // + tap: wire output `id` into the monitor's first free input; if all are used and its inputs
  // follow an `inputs` count (ports_from = "inputs"), add one. One undo step.
  const tap = (monitor: string, id: string) => {
    const [source, sourceHandle] = id.split("\u0000");
    const node = nodes.find((n) => n.id === monitor);
    if (!node || !isBlock(node)) return;
    const inputs = portsOf(node).inputs.map((p) => p.id);
    const used = new Set(edges.filter((e) => e.target === monitor).map((e) => e.targetHandle));
    if (edges.some((e) => e.target === monitor && e.source === source && e.sourceHandle === sourceHandle)) return;
    let input = inputs.find((i) => !used.has(i));
    if (!input) {
      if (node.data.def.ports_from !== "inputs") return log("warn", `${monitor} has no free input to tap ${source}.${sourceHandle}`);
      if (inputs.length >= 26) return log("warn", `${monitor} has 26 inputs, all wired: remove one to tap ${source}.${sourceHandle}`);
      input = String.fromCharCode(97 + inputs.length);
      setNodes((ns) => ns.map((n) => (n.id === monitor && isBlock(n) ? { ...n, data: { ...n.data, params: { ...n.data.params, inputs: inputs.length + 1 } } } : n)));
    }
    setEdges((es) => [...es, makeWire({ source, sourceHandle, target: monitor, targetHandle: input }, "signal")]);
    log("info", `tap ${source}.${sourceHandle} → ${monitor}.${input}`);
  };
  return { monitors, taps, tap };
}
