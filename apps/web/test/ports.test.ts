import assert from "node:assert/strict";
import { test } from "node:test";
import { inForm, parsePlaceholders, parseSigns, portHint, portLabel, wireMismatch } from "../src/ports.ts";
import type { BlockDef, PortDef } from "../src/protocol.ts";

const state: PortDef = {
  id: "state",
  type: "vector",
  dimension: 2,
  components: [{ id: "x", unit: "m" }, { id: "angle", unit: "rad" }],
};

test("vector and legacy numbers ports connect as one value when dimensions and units fit", () => {
  assert.equal(
    wireMismatch(state, {
      id: "state",
      type: "numbers",
      components: [{ id: "horizontal", unit: "m" }, { id: "heading", unit: "rad" }],
    }),
    null,
  );
  assert.equal(wireMismatch(state, { id: "state", type: "any" }), null);
  assert.match(wireMismatch(state, { id: "next", type: "step" }) ?? "", /vector port to a step port/);
});

test("vector dimensions and per-component units are checked while wiring", () => {
  assert.equal(
    wireMismatch(state, { id: "state", type: "vector", dimension: 3 }),
    "carries a vector of 2 values into a port expecting 3",
  );
  assert.equal(
    wireMismatch(state, { id: "state", type: "vector", components: [{ id: "x", unit: "cm" }, { id: "a", unit: "rad" }] }),
    "carries vector component 0 in m into a port in cm",
  );
});

test("vector port labels and hover hints expose component metadata", () => {
  assert.equal(portLabel(state), "state [2]");
  assert.equal(portHint(state), "Ordered numeric vector (2 values): x (m), angle (rad)");
});

test("parsePlaceholders extracts unique tokens and ignores escaped braces", () => {
  assert.deepEqual(parsePlaceholders("Hello {name}, your id is {id} and {{escaped}}! Also { name }."), ["name", "id"]);
  assert.deepEqual(parsePlaceholders("constant string without braces"), []);
  assert.deepEqual(parsePlaceholders("{x} + {y} = {x}"), ["x", "y"]);
});

test("inForm dynamicDef resolves template, inputs count, and signs", () => {
  const sumDef: BlockDef = {
    type: "sum",
    label: "Sum",
    kind: "function",
    inputs: [{ id: "a", type: "signal" }, { id: "b", type: "signal" }],
    outputs: [{ id: "y", type: "signal" }],
    params: [{ id: "signs", type: "text", default: "++" }],
    ports_from: "signs",
  };
  const sum3 = inForm(sumDef, { signs: "+-+" });
  assert.equal(sum3.inputs.length, 3);
  assert.deepEqual(sum3.inputs.map((p) => p.id), ["a", "b", "c"]);

  const fmtDef: BlockDef = {
    type: "format",
    label: "Format",
    kind: "function",
    inputs: [],
    outputs: [{ id: "y", type: "text" }],
    params: [{ id: "template", type: "text", default: "Hello {name}" }],
    ports_from: "template",
  };
  const fmtResolved = inForm(fmtDef, { template: "Coords: ({x}, {y}, {z})" });
  assert.deepEqual(fmtResolved.inputs.map((p) => p.id), ["x", "y", "z"]);
  assert.equal(fmtResolved.inputs[0].type, "any");

  const andDef: BlockDef = {
    type: "and",
    label: "And",
    kind: "function",
    inputs: [{ id: "a", type: "signal" }, { id: "b", type: "signal" }],
    outputs: [{ id: "y", type: "signal" }],
    params: [{ id: "inputs", type: "number", default: 2 }],
    ports_from: "inputs",
  };
  const and4 = inForm(andDef, { inputs: 4 });
  assert.deepEqual(and4.inputs.map((p) => p.id), ["a", "b", "c", "d"]);
});
