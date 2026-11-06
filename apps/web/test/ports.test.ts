import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { parseInputs, portHint, portLabel, wireMismatch } from "../src/ports.ts";
import type { PortDef } from "../src/protocol.ts";

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

test("parseInputs matches the engine's shared inputs_from test vector", () => {
  const file = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../../engine/tests/data/inputs_from.json");
  const cases = JSON.parse(fs.readFileSync(file, "utf8")) as { f: string; inputs: string[] }[];
  for (const c of cases) {
    assert.deepEqual(parseInputs(c.f), c.inputs, `for formula: ${c.f}`);
  }
});
