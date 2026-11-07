import assert from "node:assert/strict";
import { test } from "node:test";
import {
  bindParamToTopLevel,
  commitParamValue,
  exposeComponentPort,
  paramUnit,
  parseParamBinding,
  portActionFor,
  resolveParamNumber,
  unbindParam,
} from "../src/popover/popover.ts";
import { createFakeStudio } from "./fake.ts";

test("popover: commit numeric param calls setParam('R1.R', v)", async () => {
  const api = createFakeStudio();
  await commitParamValue(api, "R1", "R", 47.5);
  assert.deepEqual(api.calls.at(-1), {
    method: "setParam",
    args: ["R1.R", "47.5"],
  });
  const r1 = api.model.components.find((c) => c.name === "R1")!;
  assert.equal(r1.params.R, "47.5");
  assert.equal(paramUnit("R", api.model), "Ohm");
});

test("popover: '=' binds a param to a top-level parameter and produces the right setParam calls", async () => {
  const api = createFakeStudio();
  const r1 = api.model.components.find((c) => c.name === "R1")!;
  assert.equal(r1.params.R, "10");

  const ok = await bindParamToTopLevel(api, "R1", "R", "R_load", r1.params.R);
  assert.equal(ok, true);

  assert.deepEqual(api.calls.slice(-2), [
    { method: "setParam", args: ["R_load", "10"] },
    { method: "setParam", args: ["R1.R", "R_load"] },
  ]);

  const topParam = api.model.params.find((p) => p.id === "R_load");
  assert.ok(topParam, "created top-level parameter R_load");
  assert.equal(topParam.default, 10);

  const parsed = parseParamBinding("R", r1.params.R);
  assert.equal(parsed.bound, "R_load");
  assert.equal(parsed.modifierText, "R = R_load");
  assert.equal(resolveParamNumber("R", r1.params.R, api.model), 10);

  // Rejects invalid identifier names
  const bad = await bindParamToTopLevel(api, "R1", "R", "123 bad", "10");
  assert.equal(bad, false);

  // Unbinding restores the resolved numeric value
  await unbindParam(api, "R1", "R", "R_load");
  assert.equal(r1.params.R, "10");
});

test("popover: port actions map causal inputs to + Input and physical/output ports to + Probe", async () => {
  const api = createFakeStudio();
  const r1 = api.model.components.find((c) => c.name === "R1")!;
  const pPort = r1.ports.find((p) => p.id === "p")!;
  const uPort = r1.ports.find((p) => p.id === "u")!;

  const pAction = portActionFor(r1, pPort, api.model.connectors, api.model.entries);
  assert.equal(pAction.kind, "output");
  assert.equal(pAction.buttonLabel, "+ Probe");
  assert.equal(pAction.unit, "V");

  const uAction = portActionFor(r1, uPort, api.model.connectors, []);
  assert.equal(uAction.kind, "input");
  assert.equal(uAction.buttonLabel, "+ Input");

  await exposeComponentPort(api, r1, pPort);
  assert.deepEqual(api.calls.at(-1), {
    method: "addEntry",
    args: ["output", "R1.p"],
  });
});
