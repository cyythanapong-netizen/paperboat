import assert from "node:assert/strict";
import { test } from "node:test";
import { analysisRequest, analysisTarget } from "../src/analyses.ts";
import { makeNode, type FlowNode, type Wire } from "../src/flowModel.ts";

const def = (type: string, inputs: string[], outputs: string[], kind: "function" | "monitor" = "function") => ({
  type, label: type, kind, inputs: inputs.map((id) => ({ id })), outputs: outputs.map((id) => ({ id })), params: [],
});
const wire = (s: string, sh: string, t: string, th: string): Wire => ({ id: `${s}.${sh}-${t}.${th}`, source: s, sourceHandle: sh, target: t, targetHandle: th, type: "wire" });

// r → sum(a) ; sum → pid → plant → scope, plant → sum(b): the loop is a subsystem holding sum, pid, plant.
const group = { id: "loop", type: "frame", position: { x: 0, y: 0 }, data: { kind: "subsystem", label: "loop" } } as unknown as FlowNode;
const inLoop = (n: FlowNode) => ({ ...n, parentId: "loop" }) as FlowNode;
const nodes: FlowNode[] = [
  makeNode(def("step", [], ["y"]), "r", 0, 0),
  group,
  inLoop(makeNode(def("sum", ["a", "b"], ["y"]), "sum", 0, 0)),
  inLoop(makeNode(def("pid", ["e"], ["u"]), "pid", 0, 0)),
  inLoop(makeNode(def("plant", ["u"], ["y", "x"]), "plant", 0, 0)),
  inLoop(makeNode(def("scope", ["in"], [], "monitor"), "scope", 0, 0)),
];
const edges: Wire[] = [
  wire("r", "y", "sum", "a"),
  wire("sum", "y", "pid", "e"),
  wire("pid", "u", "plant", "u"),
  wire("plant", "y", "sum", "b"),
  wire("plant", "y", "scope", "in"),
];

test("a subsystem's boundary: inputs fed from outside, outputs read outside, by a monitor, or by nothing", () => {
  const t = analysisTarget(nodes, edges, ["loop"], "loop");
  assert.deepEqual(t.ids.sort(), ["pid", "plant", "sum"]);
  assert.deepEqual(t.inputs, [["sum", "a"]]);
  assert.deepEqual(t.outputs, [["plant", "y"], ["plant", "x"]]);
});

test("a selection of one block is its own inputs and outputs; the request keeps only the wires inside", () => {
  const t = analysisTarget(nodes, edges, ["pid", "plant"], "2 blocks");
  assert.deepEqual(t.inputs, [["pid", "e"]]);
  assert.deepEqual(t.outputs, [["plant", "y"], ["plant", "x"]]);
  const graph = { nodes: nodes.filter((n) => n.type === "block").map((n) => ({ id: n.id, type: "x" })), edges: edges.map((e) => ({ from: [e.source, e.sourceHandle!] as [string, string], to: [e.target, e.targetHandle!] as [string, string] })) };
  const req = analysisRequest("1", "control/linear", graph, t, {});
  assert.deepEqual(req.graph.nodes.map((n) => n.id), ["pid", "plant"]);
  assert.deepEqual(req.graph.edges, [{ from: ["pid", "u"], to: ["plant", "u"] }]);
  assert.equal("options" in req, false);
});
