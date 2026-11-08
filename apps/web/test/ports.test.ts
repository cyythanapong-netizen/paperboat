import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { inForm, parseInputs, parseNames, parsePlaceholders, parseSigns, portHint, portLabel, wireMismatch } from "../src/ports.ts";
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
  const and1 = inForm(andDef, { inputs: 1 });
  assert.deepEqual(and1.inputs.map((p) => p.id), ["a"]);

  const scopeDef: BlockDef = {
    type: "scope",
    label: "Scope",
    kind: "monitor",
    inputs: [{ id: "a", type: "any" }, { id: "b", type: "any" }],
    outputs: [],
    params: [{ id: "inputs", type: "number", default: 2 }],
    ports_from: "inputs",
  };
  const scope1 = inForm(scopeDef, { inputs: 1 });
  assert.deepEqual(scope1.inputs.map((p) => p.id), ["a"]);
});

test("parseInputs matches the engine's shared inputs_from test vector", () => {
  const file = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../../engine/tests/data/inputs_from.json");
  const cases = JSON.parse(fs.readFileSync(file, "utf8")) as { f: string; inputs: string[] }[];
  for (const c of cases) {
    assert.deepEqual(parseInputs(c.f), c.inputs, `for formula: ${c.f}`);
  }
});

test("an elementwise block's broadcast port takes a vector wire; a plain signal port refuses one (#164)", () => {
  const vec: PortDef = { id: "y", type: "vector", dimension: 3 };
  assert.equal(wireMismatch(vec, { id: "u", type: "signal", broadcast: true }), null);
  assert.equal(wireMismatch({ id: "y", type: "signal", broadcast: true }, { id: "u", type: "vector", dimension: 2 }), null);
  assert.equal(wireMismatch(vec, { id: "u", type: "signal" }), "joins a vector port to a signal port");
  assert.equal(wireMismatch({ ...state }, { id: "u", type: "signal", unit: "m", broadcast: true }), "carries vector component 1 in rad into a port in m");
});

test("names: identifiers split on spaces and commas, each once, none of the other side's ids", () => {
  assert.deepEqual(parseNames("x, v theta x 1a y", ["y"]), ["x", "v", "theta"]);
  assert.deepEqual(parseNames(undefined), []);
});

// The same fixture the engine's vector_test reads: what the catalog sends for a block, and the
// ports its params give it. The engine checks the def against its catalog, so neither side drifts.
const here = path.dirname(fileURLToPath(import.meta.url));
const fixture: { type: string; params: Record<string, string | number>; inputs: string[]; outputs: string[]; def: BlockDef }[] = JSON.parse(
  fs.readFileSync(path.join(here, "../../../engine/tests/data/ports.json"), "utf8"),
);

test("dynamic ports match the engine's on the shared fixture", () => {
  for (const c of fixture) {
    const def = inForm(c.def, c.params);
    assert.deepEqual(def.inputs.map((p) => p.id), c.inputs, c.type);
    assert.deepEqual(def.outputs.map((p) => p.id), c.outputs, c.type);
  }
  const mux = inForm(fixture[0].def, fixture[0].params);
  assert.deepEqual(mux.inputs.map((p) => p.unit), ["m", "m/s", "rad"]);
  assert.equal(mux.outputs[0].dimension, 3);
  assert.deepEqual(mux.outputs[0].components?.[2], { id: "theta", unit: "rad" });
});
