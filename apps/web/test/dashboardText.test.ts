// Run with `npm test` in apps/web (Node's own test runner, types stripped).
import assert from "node:assert/strict";
import { test } from "node:test";
import { formatText, inlineInText, mapTexts, renameInText, textUsing } from "../src/dashboardLayout.ts";

const parts = (text: string, latest: Record<string, unknown> = {}, vars: Record<string, unknown> = {}) =>
  formatText(text, latest, vars).map((p) => (typeof p === "string" ? p : `${p.variable ? "var:" : "block:"}${p.name}=${p.value}`));

test("a variable's value shows, and a missing name is a dash", () => {
  assert.deepEqual(parts("Limit: {max_speed}", {}, { max_speed: 3 }), ["Limit: ", "var:max_speed=3"]);
  assert.deepEqual(parts("{nope}"), ["block:nope=–"]);
});

test("a block wins over a variable of the same name", () => {
  const [p] = formatText("{x}", { x: 1 }, { x: 2 });
  assert.deepEqual(p, { name: "x", value: "1", variable: false, shadowed: true });
});

test(":.2f applies to variables, booleans read on/off", () => {
  assert.deepEqual(parts("{g:.2f}", {}, { g: 9.80665 }), ["var:g=9.81"]);
  assert.deepEqual(parts("{on}", {}, { on: true }), ["var:on=on"]);
});

test("braces stay escaped", () => {
  assert.deepEqual(parts("{{x}} {x}", {}, { x: 1 }), ["{x} ", "var:x=1"]);
});

test("renaming rewrites {old} and {old:spec}, not {{old}}", () => {
  assert.equal(renameInText("{a} {a:.1f} {{a}} {ab}", "a", "b"), "{b} {b:.1f} {{a}} {ab}");
});

test("deleting inlines the value", () => {
  assert.equal(inlineInText("v={a:.1f} {{a}}", "a", 2.55), "v=2.5 {{a}}".replace("2.5", (2.55).toFixed(1)));
});

test("labels using a variable are found and rewritten", () => {
  const layout = { instruments: [], decorations: [{ kind: "label", text: "Max {m}" }, { kind: "hline" }, { kind: "section", text: "S" }] } as never;
  assert.deepEqual(textUsing(layout, "m").map((d) => d.index), [0]);
  const next = mapTexts(layout, (t) => renameInText(t, "m", "n")) as { decorations: { text?: string }[] };
  assert.equal(next.decorations[0].text, "Max {n}");
  assert.equal(mapTexts(layout, (t) => t), layout);
});
