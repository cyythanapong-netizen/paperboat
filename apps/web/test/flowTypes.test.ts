import assert from "node:assert/strict";
import { test } from "node:test";
import { byFlow, flowLabel, flowsLabel, inferFlow, notHere, offeredFlows, sheetFlow, worksIn } from "../src/flowTypes.ts";
import { makeNode, type FlowNode } from "../src/flowModel.ts";
import type { BlockDef } from "../src/protocol.ts";
import type { GroupNode } from "../src/groups.tsx";

const gain: BlockDef = { type: "gain", label: "Gain", kind: "function", inputs: [{ id: "u" }], outputs: [{ id: "y" }], params: [] };
const pid: BlockDef = { ...gain, type: "pid", label: "PID", flows: ["signal"] };
const wait: BlockDef = { type: "wait", label: "Wait", kind: "function", inputs: [{ id: "in", type: "step" }], outputs: [{ id: "next", type: "step" }], params: [], flows: ["step"] };
const resistor: BlockDef = { ...gain, type: "resistor", label: "Resistor", flows: ["model"] };
const magicIf: BlockDef = {
  type: "if",
  label: "If",
  kind: "function",
  inputs: [{ id: "cond" }, { id: "a" }, { id: "b" }],
  outputs: [{ id: "y" }],
  params: [{ id: "form", type: "enum", default: "signal", options: ["signal", "step"] }],
  forms: [
    { name: "signal", inputs: [{ id: "cond" }, { id: "a" }, { id: "b" }], outputs: [{ id: "y" }] },
    { name: "step", inputs: [{ id: "in", type: "step" }, { id: "cond" }], outputs: [{ id: "yes", type: "step" }, { id: "no", type: "step" }] },
  ],
};

const frame = (id: string, kind: "story" | "subsystem" | "visual", parentId?: string): GroupNode => ({
  id,
  type: "frame",
  position: { x: 0, y: 0 },
  data: { kind, label: id },
  ...(parentId ? { parentId } : {}),
}) as GroupNode;

test("a block works in the flow types it names; one that names none (an older catalog) in every one", () => {
  assert.ok(worksIn(gain, "signal") && worksIn(gain, "step") && worksIn(gain, "model"));
  assert.ok(worksIn(pid, "signal"));
  assert.ok(!worksIn(pid, "step"));
  assert.ok(worksIn(wait, "step") && !worksIn(wait, "signal"));
  assert.equal(flowLabel("step"), "Step flow");
  assert.equal(flowsLabel(["signal", "step"]), "Signal flow or Step flow");
  assert.equal(flowsLabel(["signal", "step", "model"]), "Signal flow, Step flow or Model flow");
  assert.equal(notHere(wait, "signal"), "Works only in a Step flow, not in this Signal flow");
});

test("the palette on a sheet: blocks that work in it, and the rest apart; station objects are never held back", () => {
  const items = [gain, pid, wait, resistor].map((def) => ({ kind: "block" as const, def }));
  const signal = byFlow(items, "signal");
  assert.deepEqual(signal.fits.map((i) => i.def.type), ["gain", "pid"]);
  assert.deepEqual(signal.elsewhere.map((i) => i.def.type), ["wait", "resistor"]);
  const step = byFlow(items, "step");
  assert.deepEqual(step.fits.map((i) => i.def.type), ["gain", "wait"]);
  assert.deepEqual(step.elsewhere.map((i) => i.def.type), ["pid", "resistor"]);
  const objects = byFlow([{ kind: "object", def: { type: "cart" } }], "model");
  assert.equal(objects.fits.length, 1);
});

test("the flow types offered: hello's, in order; an older backend: Signal flow, and Step flow if a block has step ports", () => {
  assert.deepEqual(offeredFlows(["step", "signal"], [gain]), ["signal", "step"]);
  assert.deepEqual(offeredFlows(["model"], [gain]), ["model"]);
  assert.deepEqual(offeredFlows(undefined, [gain, pid]), ["signal"]);
  assert.deepEqual(offeredFlows(undefined, [gain, wait]), ["signal", "step"]);
  assert.deepEqual(offeredFlows([], [magicIf]), ["signal", "step"]); // a magic block with a step form
});

test("a sheet whose type wasn't chosen: a Step flow if a block on it has step ports, or a story is on it", () => {
  assert.equal(inferFlow([]), "signal");
  assert.equal(inferFlow([makeNode(gain, "g", 0, 0), makeNode(pid, "p", 0, 0)]), "signal");
  assert.equal(inferFlow([makeNode(gain, "g", 0, 0), makeNode(wait, "w", 0, 0)]), "step");
  assert.equal(inferFlow([makeNode(magicIf, "if", 0, 0)]), "signal"); // in its signal form
  assert.equal(inferFlow([makeNode(magicIf, "if", 0, 0, { form: "step" })]), "step");
  assert.equal(inferFlow([frame("s", "story")]), "step");
  const lost = { ...makeNode(wait, "w", 0, 0) };
  lost.data = { ...lost.data, missing: true };
  assert.equal(inferFlow([lost]), "signal"); // a block the catalog lacks says nothing
});

test("the open sheet: a subsystem is its sheet's type; a story's inside (and what is in it) a Signal flow", () => {
  const nodes: FlowNode[] = [frame("sub", "subsystem"), frame("story", "story"), frame("deep", "subsystem", "story"), frame("box", "visual", "deep")];
  assert.deepEqual(sheetFlow("step", null, nodes), { flow: "step" });
  assert.deepEqual(sheetFlow("step", "sub", nodes), { flow: "step" });
  assert.deepEqual(sheetFlow("step", "story", nodes), { flow: "signal", story: "story" });
  assert.deepEqual(sheetFlow("step", "deep", nodes), { flow: "signal", story: "story" });
  assert.deepEqual(sheetFlow("signal", "gone", nodes), { flow: "signal" });
});
