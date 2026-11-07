import assert from "node:assert/strict";
import { test } from "node:test";
import { CORNER, DASH, FILL, FONT, iconNodes, iconValid, plateSize, SIZE, STROKE, WEIGHT } from "../src/icon.ts";
import type { IconDef, Prim } from "../src/protocol.ts";
import { Ground, Inertia, Resistor } from "./fixtures/icons.ts";

const tokenValues = new Set<string | number>([
  ...Object.values(STROKE), ...Object.values(WEIGHT), ...Object.values(DASH), ...Object.values(FILL),
  ...Object.values(CORNER), ...Object.values(FONT), ...SIZE, "round", "none", "middle", "central",
]);
const geometry = /^-?\d+(\.\d+)?$|^(-?\d+(\.\d+)?,-?\d+(\.\d+)? ?)+$|^M[-\d.,]+A[-\d.,]+ 0 [01] 0 [-\d.,]+$/;
const GEOMETRY_ATTRS = new Set(["x", "y", "width", "height", "cx", "cy", "points", "d"]);
// rx / ry are geometry for ellipse and a corner token for rect: both are numbers.

/** Every attribute value an icon writes is a token value or a number that passed the grid. */
function checkAttrs(def: IconDef) {
  const nodes = iconNodes(def, "t");
  assert.ok(nodes, "drawable");
  for (const n of nodes) {
    for (const [k, v] of Object.entries(n.attrs)) {
      if (GEOMETRY_ATTRS.has(k) || ((k === "rx" || k === "ry") && n.tag === "ellipse")) assert.match(String(v), geometry, `${n.tag}.${k}=${v}`);
      else assert.ok(tokenValues.has(v), `${n.tag}.${k}=${v} is not a token value`);
      assert.doesNotMatch(String(v), /#|rgb\(/, `${k} holds a raw colour`);
    }
  }
}

const every = <T>(map: Record<string, T>) => Object.keys(map);

test("every primitive with every token writes only token values", () => {
  const styled = (p: Prim) => [
    ...every(STROKE).map((stroke) => ({ ...p, stroke })),
    ...every(WEIGHT).map((weight) => ({ ...p, weight })),
    ...every(DASH).map((dash) => ({ ...p, dash })),
    ...every(FILL).map((fill) => ({ ...p, fill })),
  ] as Prim[];
  const prims: Prim[] = [
    { t: "line", p: [[0, 0], [100, 100]] },
    { t: "poly", p: [[0, 0], [100, 0], [50, 100]] },
    ...every(CORNER).map((corner) => ({ t: "rect", x: 10, y: 10, w: 50, h: 40, corner }) as Prim),
    { t: "ellipse", x: 10, y: 10, w: 50, h: 40 },
    { t: "arc", x: 10, y: 10, w: 50, h: 40, a0: 0, a1: 270 },
    ...every(FONT).flatMap((font) => SIZE.map((size) => ({ t: "text", x: 50, y: 50, s: "R", font, size }) as Prim)),
  ];
  for (const p of prims.flatMap(styled)) checkAttrs({ g: [p] });
  for (const def of [Resistor, Ground, Inertia]) checkAttrs(def);
});

test("a hex, rgb(, a d path or a raw number in place of a token is refused", () => {
  const line: Prim = { t: "line", p: [[0, 0], [100, 100]] };
  const bad = (patch: object) => ({ g: [{ ...line, ...patch } as Prim] });
  for (const patch of [{ stroke: "#ff0000" }, { stroke: "rgb(1,2,3)" }, { stroke: "var(--text)" }, { weight: 2 }, { weight: "3" }, { dash: "5 5" }, { fill: "red" }, { fill: 1 }])
    assert.equal(iconValid(bad(patch)), false, JSON.stringify(patch));
  assert.equal(iconValid({ g: [{ t: "rect", x: 0, y: 0, w: 50, h: 50, corner: "9px" } as unknown as Prim] }), false);
  assert.equal(iconValid({ g: [{ t: "text", x: 50, y: 50, s: "a", font: "Comic" } as unknown as Prim] }), false);
  assert.equal(iconValid({ g: [{ t: "path", d: "M0 0L10 10" } as unknown as Prim] }), false);
});

test("unknown primitive, token, size, 2x2 or an off-grid point falls back to the plain plate", () => {
  const warn = console.warn;
  const warned: string[] = [];
  console.warn = (m: string) => warned.push(m);
  try {
    const cases: IconDef[] = [
      { g: [{ t: "squiggle" } as unknown as Prim] },
      { g: [{ t: "line", p: [[0, 0], [100, 100]], stroke: "red" } as unknown as Prim] },
      { g: [{ t: "text", x: 50, y: 50, s: "a", size: 14 } as unknown as Prim] },
      { w: 2, h: 2, g: [] },
      { g: [{ t: "line", p: [[0, 3], [100, 100]] }] },
      { g: [{ t: "line", p: [[0, 0], [105, 100]] }] },
    ];
    for (const [i, c] of cases.entries()) assert.equal(iconNodes(c, `bad${i}`), null, `case ${i}`);
    assert.equal(warned.length, cases.length);
    iconNodes(cases[0], "bad0"); // once per block type
    assert.equal(warned.length, cases.length);
    assert.equal(iconNodes(undefined, "none"), null);
    assert.equal(warned.length, cases.length, "no icon is not a warning");
  } finally {
    console.warn = warn;
  }
});

test("plates grow on the 4px grid and {param} in text is filled", () => {
  assert.deepEqual(plateSize({}), { w: 64, h: 56 });
  assert.deepEqual(plateSize({ w: 2 }), { w: 128, h: 56 });
  assert.deepEqual(plateSize({ h: 2 }), { w: 64, h: 112 });
  for (const s of [plateSize({ w: 2 }), plateSize({ h: 2 })]) assert.ok(s.w % 4 === 0 && s.h % 4 === 0);
  const text = iconNodes(Resistor, "Resistor", (t) => t.replace("{R}", "100"))!.find((n) => n.tag === "text");
  assert.equal(text?.text, "100");
});
