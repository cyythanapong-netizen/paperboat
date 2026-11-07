import assert from "node:assert/strict";
import { test } from "node:test";
import { toToml } from "../src/bindings.ts";
import { addDecoration, arrange } from "../src/dashboardLayout.ts";

test("exposed, dock and tunable reach the descriptor TOML", () => {
  const toml = toToml([{ name: "g", type: "number", value: 9.8, exposed: true, dock: true, tunable: true }, { name: "k", type: "number", value: 1 }]);
  const [g, k] = toml.split("\n\n");
  for (const flag of ["exposed", "dock", "tunable"]) assert.match(g, new RegExp(`^${flag} = true$`, "m"));
  assert.doesNotMatch(k, /exposed|dock|tunable/);
});

test("a Controls tile takes cells like the Console and the Station", () => {
  const layout = addDecoration(arrange({}, undefined, 6), "controls", "");
  assert.deepEqual(layout.decorations?.[0]?.span, [4, 3]);
  const [d] = arrange({}, layout, 6).decorations;
  assert.deepEqual([d.kind, d.w, d.h], ["controls", 4, 3]);
});
