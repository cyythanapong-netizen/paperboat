// Lane A's store: which omc expression each action sends, what the model looks like read back,
// the splice, undo's replay, and the validity words. No omc, no DOM: canned answers in, text out.

import assert from "node:assert/strict";
import { test } from "node:test";
import { Ground, Resistor, connectors } from "../../../packages/shared/test/fixtures/icons.ts";
import type { ComponentInst, StudioModel } from "../src/model/types.ts";
import type { OmcCalls } from "../src/omc/omc.ts";
import { chip } from "../src/omc/omc.ts";
import { parseValue } from "../src/omc/parse.ts";
import { restoreExprs, type Graph } from "../src/store/diff.ts";
import * as x from "../src/store/exprs.ts";
import { connectExprs, entryExprs, removeEntryExprs, removeExprs, setParamExprs } from "../src/store/plans.ts";
import { bendsOf, placementOf, readModel } from "../src/store/read.ts";
import { freshName, splicePlan, throughPorts } from "../src/store/splice.ts";
import { wireError } from "../src/store/wires.ts";

const part = (name: string, cls: string, icon: ComponentInst["icon"], px: number, py = 0): ComponentInst => ({
  name,
  class: cls,
  params: {},
  placement: { x: px, y: py, rotation: 0 },
  icon,
  ports: icon?.ports ?? [],
});
const RIN = { g: [], ports: [{ id: "u", class: "RealInput", side: "left" as const, at: 50 }] };
const ROUT = { g: [], ports: [{ id: "y", class: "RealOutput", side: "right" as const, at: 50 }] };

/** R1 - L1 wired n to p; V1.p on R1.p; Ground G; a signal S1.y into V1.v. */
const model = (): StudioModel => ({
  name: "Circuit",
  file: "/tmp/Circuit.mo",
  components: [
    part("R1", "Modelica.Electrical.Analog.Basic.Resistor", Resistor, 200),
    part("L1", "Modelica.Electrical.Analog.Basic.Inductor", Resistor, 400),
    part("G", "Modelica.Electrical.Analog.Basic.Ground", Ground, 200, 200),
    part("S1", "Modelica.Blocks.Sources.Constant", ROUT, 0, 100),
    part("V1", "Modelica.Blocks.Math.Gain", RIN, 100, 100),
  ],
  connections: [{ a: "R1.n", b: "L1.p" }],
  params: [{ id: "k", type: "number", default: 2 }],
  entries: [],
  connectors,
});

test("each action is one omc expression on the allow-list's calls", () => {
  const at = { x: 160, y: -80, rotation: 90 as const };
  assert.equal(
    x.addComponent("Circuit", "Modelica.Electrical.Analog.Basic.Resistor", "R2", at),
    "addComponent(R2, Modelica.Electrical.Analog.Basic.Resistor, Circuit, annotate=Placement(visible=true, transformation=transformation(origin={80, 40}, extent={{-10, -10}, {10, 10}}, rotation=90)))",
  );
  assert.equal(x.deleteComponent("Circuit", "R2"), "deleteComponent(R2, Circuit)");
  assert.equal(x.addConnection("Circuit", "R1.n", "L1.p"), "addConnection(R1.n, L1.p, Circuit)");
  assert.equal(x.deleteConnection("Circuit", "R1.n", "L1.p"), "deleteConnection(R1.n, L1.p, Circuit)");
  assert.equal(x.setModifier("Circuit", "R1.R", "10"), "setComponentModifierValue(Circuit, R1.R, $Code(=10))");
  assert.equal(x.setParameter("Circuit", "k", "3"), "setParameterValue(Circuit, k, 3)");
  assert.match(x.updateComponent("Circuit", "Modelica.A.B", "R1", at), /^updateComponent\(R1, Modelica\.A\.B, Circuit, annotate=Placement/);
  assert.match(x.addConnection("Circuit", "a.p", "b.n", [[0, 0], [20, 0]]), /annotate=Line\(points=\{\{0, 0\}, \{10, 0\}\}\)/);
});

test("a name that is not a name never reaches omc", () => {
  assert.throws(() => x.addComponent("Circuit", "Modelica.R", "R1; system(\"ls\")", { x: 0, y: 0, rotation: 0 }), /not a component name/);
  assert.throws(() => x.addConnection("Circuit", "R1.p", "x)", undefined), /not a connector/);
  assert.throws(() => x.deleteComponent("Circuit; x", "R1"), /not a model name/);
});

test("setParam: a dotted path is a modifier, a bare one a top-level parameter, made when missing", () => {
  const m = model();
  assert.deepEqual(setParamExprs(m, "R1.R", "10"), ["setComponentModifierValue(Circuit, R1.R, $Code(=10))"]);
  assert.deepEqual(setParamExprs(m, "k", "3"), ["setParameterValue(Circuit, k, 3)"]);
  assert.deepEqual(setParamExprs(m, "J", "0.5"), ["addComponent(J, Real, Circuit)", "setParameterValue(Circuit, J, 0.5)"]);
});

test("connect: the §4 rules answer in words, a good wire is one addConnection", () => {
  const m = model();
  assert.deepEqual(connectExprs(m, "R1.p", "G.p"), { exprs: ["addConnection(R1.p, G.p, Circuit)"] });
  assert.match((connectExprs(m, "R1.n", "L1.p") as { error: string }).error, /already wired/);
  assert.match((connectExprs(m, "R1.p", "V1.u") as { error: string }).error, /acausal|joins only/);
  assert.equal(wireError(m, "S1.y", "V1.u"), null); // output to input
  assert.equal(wireError(m, "V1.u", "S1.y"), "wire from the output to the input");
  assert.equal(wireError(m, "R1.p", "R1.p"), "a port does not join itself");
  assert.match(wireError(m, "R1.zz", "G.p") ?? "", /no port R1\.zz/);
  const two = model();
  two.components.push(part("S2", "Modelica.Blocks.Sources.Constant", ROUT, 0, 200));
  assert.equal(wireError(two, "S1.y", "S2.y"), "an output does not join an output");
});

test("a connector of the model itself connects through: an input u feeds an inner input", () => {
  const m = model();
  m.components.push(part("u", "Modelica.Blocks.Interfaces.RealInput", { g: [], ports: [{ id: "u", class: "RealInput", side: "right", at: 50 }] }, -100, 100));
  assert.equal(wireError(m, "u", "V1.u"), null);
});

test("+ Input and + Probe: a RealInput or RealOutput wired to the port; refused on a variable", () => {
  const m = model();
  const input = entryExprs(m, "input", "V1.u");
  assert.match(input[0], /^addComponent\(u, Modelica\.Blocks\.Interfaces\.RealInput, Circuit/);
  assert.equal(input[1], "addConnection(u, V1.u, Circuit)");
  const probe = entryExprs(m, "output", "S1.y");
  assert.match(probe[0], /^addComponent\(y, Modelica\.Blocks\.Interfaces\.RealOutput, Circuit/);
  assert.equal(probe[1], "addConnection(S1.y, y, Circuit)");
  assert.throws(() => entryExprs(m, "output", "R1"), /needs a port/);
  m.connections.push({ a: "u", b: "V1.u" });
  assert.deepEqual(removeEntryExprs(m, "u"), ["deleteConnection(u, V1.u, Circuit)", "deleteComponent(u, Circuit)"]);
});

test("deleting a part cuts its wires first, each once", () => {
  const m = model();
  m.connections.push({ a: "G.p", b: "R1.p" });
  assert.deepEqual(removeExprs(m, ["R1"], []), [
    "deleteConnection(R1.n, L1.p, Circuit)",
    "deleteConnection(G.p, R1.p, Circuit)",
    "deleteComponent(R1, Circuit)",
  ]);
  assert.deepEqual(removeExprs(m, [], [{ a: "R1.n", b: "L1.p" }]), ["deleteConnection(R1.n, L1.p, Circuit)"]);
});

test("splice: delete the wire, add the part, two wires through it, in that order", () => {
  const m = model();
  const through = throughPorts(Resistor.ports!, connectors, "NegativePin");
  assert.deepEqual(through, { into: "p", out: "n" });
  const plan = splicePlan("Circuit", m.connections[0], "Modelica.Electrical.Analog.Basic.Resistor", "R2", through as { into: string; out: string }, [300, 0]);
  assert.equal(plan.line, "Insert R2: R1.n -> p, n -> L1.p");
  assert.equal(plan.exprs.length, 4);
  assert.match(plan.exprs[0], /^deleteConnection\(R1\.n, L1\.p, Circuit\)$/);
  assert.match(plan.exprs[1], /^addComponent\(R2, Modelica\.Electrical\.Analog\.Basic\.Resistor, Circuit, annotate=Placement/);
  assert.equal(plan.exprs[2], "addConnection(R1.n, R2.p, Circuit)");
  assert.equal(plan.exprs[3], "addConnection(R2.n, L1.p, Circuit)");
  // stored the other way round, a reversed plan reads the same left to right
  const back = splicePlan("Circuit", { a: "L1.p", b: "R1.n" }, "Modelica.Electrical.Analog.Basic.Resistor", "R2", through as { into: string; out: string }, [300, 0], true);
  assert.equal(back.line, "Insert R2: R1.n -> p, n -> L1.p");
});

test("splice into a signal wire needs an input and an output on the part, and says so", () => {
  const through = throughPorts(Resistor.ports!, connectors, "RealOutput");
  assert.match((through as { why: string }).why, /input and an output/);
  const gain = [{ id: "u", class: "RealInput", side: "left" as const, at: 50 }, { id: "y", class: "RealOutput", side: "right" as const, at: 50 }];
  assert.deepEqual(throughPorts(gain, connectors, "RealOutput"), { into: "u", out: "y" });
  assert.match((throughPorts(Ground.ports!, connectors, "PositivePin") as { why: string }).why, /fewer than two Pin ports/);
});

test("names: R1, R2 for resistors, L for inductors, the first free number", () => {
  assert.equal(freshName("Modelica.Electrical.Analog.Basic.Resistor", ["R1", "G1"]), "R2");
  assert.equal(freshName("Modelica.Electrical.Analog.Basic.Inductor", []), "L1");
  assert.equal(freshName("Modelica.Mechanics.Rotational.Components.Inertia", ["I1", "I2"]), "I3");
});

test("undo replays the difference: wires out, parts, wires in; a splice undoes as one step", () => {
  const part = (cls: string, x0: number) => ({ class: cls, params: {}, placement: { x: x0, y: 0, rotation: 0 as const } });
  const before: Graph = { parts: { R1: part("A.Res", 0), L1: part("A.Ind", 200) }, wires: [{ a: "R1.n", b: "L1.p" }] };
  const after: Graph = {
    parts: { ...before.parts, R2: part("A.Res", 100) },
    wires: [{ a: "R1.n", b: "R2.p" }, { a: "R2.n", b: "L1.p" }],
  };
  assert.deepEqual(restoreExprs("M", after, before), [
    "deleteConnection(R1.n, R2.p, M)",
    "deleteConnection(R2.n, L1.p, M)",
    "deleteComponent(R2, M)",
    "addConnection(R1.n, L1.p, M)",
  ]);
  assert.deepEqual(restoreExprs("M", before, after), [
    "deleteConnection(R1.n, L1.p, M)",
    "addComponent(R2, A.Res, M, annotate=Placement(visible=true, transformation=transformation(origin={50, 0}, extent={{-10, -10}, {10, 10}})))",
    "addConnection(R1.n, R2.p, M)",
    "addConnection(R2.n, L1.p, M)",
  ]);
  // a moved part and an edited param
  const edited: Graph = { parts: { ...before.parts, R1: { ...before.parts.R1, placement: { x: 40, y: 0, rotation: 0 as const }, params: { R: "5" } } }, wires: before.wires };
  assert.deepEqual(restoreExprs("M", before, edited).map((e) => e.split("(")[0]), ["updateComponent", "setComponentModifierValue"]);
  assert.deepEqual(restoreExprs("M", edited, before).map((e) => e.split("(")[0]), ["updateComponent", "setComponentModifierValue"]);
  assert.deepEqual(restoreExprs("M", before, before), []);
});

test("omc's answers read as data, and a placement as the plate's centre", () => {
  assert.deepEqual(parseValue('{{A.B, R1, "c, d", "public", false}, {}}'), [["A.B", "R1", "c, d", "public", "false"], []]);
  assert.deepEqual(parseValue("{Placement(true,40.0,-20.0,-10.0,-10.0,10.0,10.0,90.0,-,-)}"), ["Placement(true,40.0,-20.0,-10.0,-10.0,10.0,10.0,90.0,-,-)"]);
  assert.deepEqual(placementOf("Placement(true,40.0,-20.0,-10.0,-10.0,10.0,10.0,90.0,-,-)"), { x: 80, y: 40, rotation: 90 });
  assert.deepEqual(placementOf("Placement(true,0.0,0.0,10.0,-10.0,-10.0,10.0,-,-,-)"), { x: 0, y: 0, rotation: 0, mirror: true });
  assert.equal(placementOf("Placement(true,1,2)"), null);
  assert.deepEqual(bendsOf("{Line(true, {0.0, 0.0}, 0, {{0, 0}, {20, 0}, {20, 20}, {40, 20}}, {0, 0, 0}, LinePattern.Solid)}"), [[40, 0], [40, -40]]);
  assert.equal(bendsOf("false"), undefined);
});

/** The answers omc would give for a two-part model: R1 placed, a resistor wired to a ground. */
function fake(): OmcCalls {
  const answers: [RegExp, string][] = [
    [/^getComponents\(/, '{{Modelica.Electrical.Analog.Basic.Resistor, R1, "", "public", false}, {Modelica.Electrical.Analog.Basic.Ground, G, "", "public", false}, {Real, k, "", "public", false}}'],
    [/^getComponentAnnotations\(/, "{{Placement(true,40.0,-20.0,-10.0,-10.0,10.0,10.0,-,-)}, {}, {}}"],
    [/^getConnectionCount\(/, "1"],
    [/^getNthConnection\(M, 1\)/, '{"R1.p", "G.p", ""}'],
    [/^getNthConnectionAnnotation\(/, "false"],
    [/^getComponentModifierNames\(M, "R1"\)/, '{"R"}'],
    [/^getComponentModifierNames\(/, "{}"],
    [/^getComponentModifierValue\(M, R1\.R\)/, '"10"'],
    [/^getClassRestriction\(/, '"model"'],
    [/^getParameterNames\(/, '{"k"}'],
    [/^getParameterValue\(M, "k"\)/, '"2.5"'],
  ];
  return {
    async call(expr: string) {
      const hit = answers.find(([re]) => re.test(expr));
      if (!hit) throw new Error(`unexpected omc call: ${expr}`);
      return hit[1];
    },
    async op<T>(name: string, args: Record<string, unknown> = {}) {
      assert.equal(name, "icon");
      const cls = String(args.class);
      return { icon: cls.endsWith("Resistor") ? Resistor : cls.endsWith("Ground") ? Ground : { g: [] }, connectors } as T;
    },
  } as OmcCalls;
}

test("reading a model: parts with place, modifiers and ports, wires, top-level parameters", async () => {
  const m = await readModel(fake(), "M", "/tmp/M.mo", new Map());
  assert.equal(m.name, "M");
  assert.equal(m.file, "/tmp/M.mo");
  assert.deepEqual(m.components.map((c) => c.name), ["R1", "G"]); // `Real k` is a parameter, not a part
  assert.deepEqual(m.components[0].placement, { x: 80, y: 40, rotation: 0 });
  assert.deepEqual(m.components[0].params, { R: "10" });
  assert.deepEqual(m.components[0].ports.map((p) => p.id), ["p", "n"]);
  assert.equal(m.components[1].placement.x % 20, 0); // placed on the grid when the file has no Placement
  assert.deepEqual(m.connections, [{ a: "R1.p", b: "G.p" }]);
  assert.deepEqual(m.params, [{ id: "k", type: "number", default: 2.5 }]);
  assert.ok(m.connectors.PositivePin);
});

test("the top bar's chip says the omc version and state", () => {
  assert.equal(chip({ state: "ready", version: "OpenModelica 1.27.0~dev-5-g5f1d2c5" }), "omc 1.27.0 · ready");
  assert.equal(chip({ state: "checking", version: "" }), "omc · checking");
  assert.equal(chip({ state: "failed", version: "OpenModelica 1.27.0" }), "omc 1.27.0 · failed");
});
