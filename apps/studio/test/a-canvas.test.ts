// The canvas's pure parts: an icon turned and mirrored with its ports, the library's search and
// the tree's questions to omc.

import assert from "node:assert/strict";
import { test } from "node:test";
import { Ground, Inertia, Resistor } from "../../../packages/shared/test/fixtures/icons.ts";
import { iconValid } from "../../../packages/shared/src/icon.ts";
import { centreFor, plateOf, squareUp, topLeft } from "../src/canvas/geometry.ts";
import { rotateIcon } from "../src/canvas/rotate.ts";
import { children, flat, search } from "../src/library/classes.ts";
import type { ComponentInst } from "../src/model/types.ts";
import type { OmcCalls } from "../src/omc/omc.ts";

const sides = (icon: typeof Resistor) => icon.ports!.map((p) => `${p.id}:${p.side}@${p.at}`);

test("a part turned 90 degrees anticlockwise takes its pins round with it", () => {
  const turned = rotateIcon(Resistor, { rotation: 90 });
  assert.equal(turned.w, undefined); // 2x1 becomes 1x2
  assert.equal(turned.h, 2);
  assert.deepEqual(sides(turned), ["p:bottom@50", "n:top@50"]);
  assert.ok(iconValid(turned));
  const half = rotateIcon(Resistor, { rotation: 180 });
  assert.deepEqual(sides(half), ["p:right@50", "n:left@50"]);
  assert.deepEqual(sides(rotateIcon(Resistor, { rotation: 270 })), ["p:top@50", "n:bottom@50"]);
  assert.deepEqual(sides(rotateIcon(Ground, { rotation: 90 })), ["p:left@50"]); // top goes to the left
});

test("mirroring swaps left and right, and turning four times is the part again", () => {
  assert.deepEqual(sides(rotateIcon(Resistor, { rotation: 0, mirror: true })), ["p:right@50", "n:left@50"]);
  let icon = Inertia;
  for (let i = 0; i < 4; i++) icon = rotateIcon(icon, { rotation: 90 });
  assert.deepEqual(icon.g, Inertia.g);
  assert.deepEqual(icon.ports, Inertia.ports);
  assert.equal(rotateIcon(Resistor, { rotation: 0 }), Resistor); // nothing to do, same object
});

test("a turned icon stays on the grid, so the plate still draws it", () => {
  for (const rotation of [0, 90, 180, 270] as const)
    for (const icon of [Resistor, Ground, Inertia]) assert.ok(iconValid(rotateIcon(icon, { rotation })), `${rotation}`);
});

test("a placement is the plate's centre; the node sits half a plate up and left", () => {
  const part: ComponentInst = { name: "R1", class: "R", params: {}, placement: { x: 200, y: 100, rotation: 0 }, icon: Resistor, ports: Resistor.ports! };
  assert.deepEqual(plateOf(part), { w: 128, h: 56 });
  assert.deepEqual(topLeft(part), [136, 72]);
  assert.deepEqual(centreFor(part, { x: 136, y: 72 }), [200, 100]);
  assert.deepEqual(plateOf({ ...part, placement: { x: 0, y: 0, rotation: 90 } }), { w: 64, h: 112 });
});

test("a stored bend is squared up with the port it leaves", () => {
  assert.deepEqual(squareUp([100, 103], [[140, 100], [140, 200]], [200, 197]), [[100, 103], [140, 103], [140, 197], [200, 197]]);
  assert.deepEqual(squareUp([0, 0], [], [10, 10]), [[0, 0], [10, 10]]);
});

test("search finds a part by its name first, then its path", () => {
  const names = ["Modelica.Electrical.Analog.Basic.Resistor", "Modelica.Electrical.Analog.Basic.VariableResistor", "Modelica.Electrical.Polyphase.Basic.Resistor", "Modelica.Electrical.Analog.Basic.Ground", "Modelica.Mechanics.Rotational.Components.Inertia"];
  assert.deepEqual(search(names, "resistor").slice(0, 2), ["Modelica.Electrical.Analog.Basic.Resistor", "Modelica.Electrical.Polyphase.Basic.Resistor"]);
  assert.equal(search(names, "resistor").length, 3);
  assert.deepEqual(search(names, "rotational inertia"), []);
  assert.deepEqual(search(names, "Rotational.Components"), ["Modelica.Mechanics.Rotational.Components.Inertia"]);
  assert.deepEqual(search(names, ""), []);
});

function tree(): OmcCalls {
  const answers: [RegExp, string][] = [
    [/^getClassNames\(Modelica\.Electrical\)/, "{Analog, UsersGuide, Examples, Machines}"],
    [/^getClassRestriction\(Modelica\.Electrical\.(Analog|Machines)\)/, '"package"'],
    [/^getClassNames\(Modelica\.Electrical\.Analog\)/, "{Basic, Partial}"],
    [/^getClassRestriction\(Modelica\.Electrical\.Analog\.Basic\)/, '"package"'],
    [/^getClassRestriction\(Modelica\.Electrical\.Analog\.Partial\)/, '"model"'],
    [/^isPartial\(Modelica\.Electrical\.Analog\.Partial\)/, "true"],
    [/^getClassNames\(Modelica, recursive=true\)/, "{Modelica, Modelica.Electrical, Modelica.Electrical.Analog.Basic.Resistor, Modelica.Electrical.UsersGuide.Foo, Modelica.Electrical.Examples.Demo, Modelica.Electrical.Analog.Interfaces.OnePort, modelica.lower}"],
  ];
  return {
    async call(expr: string) {
      const hit = answers.find(([re]) => re.test(expr));
      return hit ? hit[1] : '""';
    },
    async op() {
      throw new Error("no ops");
    },
  } as unknown as OmcCalls;
}

test("the tree opens a package at a time: packages to open, parts to place, no demos or partial classes", async () => {
  const omc = tree();
  assert.deepEqual((await children(omc, "Modelica.Electrical")).map((e) => [e.name, e.kind]), [["Analog", "package"], ["Machines", "package"]]);
  assert.deepEqual((await children(omc, "Modelica.Electrical.Analog")).map((e) => [e.name, e.kind]), [["Basic", "package"]]);
});

test("the flat index drops packages, guides, demos and interfaces, and lowercase helpers", async () => {
  assert.deepEqual(await flat(tree(), "Modelica"), ["Modelica.Electrical.Analog.Basic.Resistor"]);
});
