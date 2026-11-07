import assert from "node:assert/strict";
import { test } from "node:test";
import { iconNodes, iconValid, type IconDef, type Prim } from "@paperboat/shared";
import {
  addPrimToIcon,
  applyPlateSize,
  buildLinkedBlockDef,
  movePrimBy,
  PLATE_SIZES,
  plateSizeKey,
  PRIM_KINDS,
  removePrimAt,
  sanitizeIconDef,
  setPortPlacement,
  snapTo5,
  syncPortsWithEntries,
  updatePrimTokens,
} from "../src/sheet/look.ts";
import { createFakeStudio } from "./fake.ts";

test("look: snapTo5 snaps coordinates to 0..100 multiples of 5", () => {
  assert.equal(snapTo5(0), 0);
  assert.equal(snapTo5(-12), 0);
  assert.equal(snapTo5(2), 0);
  assert.equal(snapTo5(3), 5);
  assert.equal(snapTo5(12.4), 10);
  assert.equal(snapTo5(12.6), 15);
  assert.equal(snapTo5(103), 100);
  assert.equal(snapTo5(Number.NaN), 50);
});

test("look: every primitive kind, move, token edit, plate size and port placement passes shared icon.ts validation", async () => {
  const api = createFakeStudio();
  let icon: IconDef = syncPortsWithEntries(api.model.icon, api.model.entries);
  assert.equal(iconValid(icon), true);

  for (const kind of PRIM_KINDS) {
    icon = addPrimToIcon(icon, kind, {
      stroke: "ink",
      weight: "normal",
      dash: "solid",
      fill: "raised",
    });
    assert.equal(iconValid(icon), true, `valid after adding ${kind}`);
    assert.ok(iconNodes(icon, "TestModel"), `drawable after adding ${kind}`);
  }

  // Move each primitive by off-grid deltas and verify every coordinate snaps to 5
  icon = {
    ...icon,
    g: icon.g.map((p) => movePrimBy(p, 7, -13)),
  };
  assert.equal(iconValid(icon), true, "valid after moving primitives");

  // Update tokens on each primitive and ensure invalid tokens are ignored
  icon = {
    ...icon,
    g: icon.g.map((p) =>
      updatePrimTokens(p, {
        stroke: "muted",
        weight: "wire",
        dash: "axis",
        fill: "sunken",
        corner: "md",
        font: "mono",
        size: 15,
        x: 13,
        y: 27,
      }),
    ),
  };
  assert.equal(iconValid(icon), true, "valid after token and position edits");

  // Plate sizes: 1x1, 2x1, 1x2 only
  for (const sz of PLATE_SIZES) {
    const sized = applyPlateSize(icon, sz);
    assert.equal(plateSizeKey(sized), sz);
    assert.equal(iconValid(sized), true, `valid plate size ${sz}`);
  }

  // 2x2 is sanitized back to 1x1
  const bad2x2 = sanitizeIconDef({ ...icon, w: 2, h: 2 }, api.model.entries);
  assert.equal(iconValid(bad2x2), true);
  assert.notDeepEqual([bad2x2.w, bad2x2.h], [2, 2]);

  // Port placement snaps 'at' to multiples of 5
  const placed = setPortPlacement(icon, api.model.entries, "u", "top", 33);
  const uPort = placed.ports?.find((p) => p.id === "u");
  assert.equal(uPort?.side, "top");
  assert.equal(uPort?.at, 35);
  assert.equal(iconValid(placed), true);

  // Removing a primitive keeps icon valid
  const beforeLen = placed.g.length;
  const afterRemove = removePrimAt(placed, 0);
  assert.equal(afterRemove.g.length, beforeLen - 1);
  assert.equal(iconValid(afterRemove), true);

  // Save via StudioApi
  await api.setIcon(afterRemove);
  assert.deepEqual(api.calls.at(-1), {
    method: "setIcon",
    args: [afterRemove],
  });
  assert.equal(iconValid(api.model.icon), true);
});

test("sheet: linked block preview and station props / entries wire through fake StudioApi", async () => {
  const api = createFakeStudio();
  await api.setParam("R_nom", "25");
  assert.equal(api.model.params.find((p) => p.id === "R_nom")?.default, 25);

  const blockDef = buildLinkedBlockDef(api.model);
  assert.equal(blockDef.linked, true);
  assert.deepEqual(blockDef.inputs.map((i) => [i.id, i.unit]), [["u", "V"]]);
  assert.deepEqual(blockDef.outputs.map((o) => [o.id, o.unit]), [["i_L", "A"]]);
  assert.equal(iconValid(blockDef.view?.icon), true);

  await api.removeEntry("i_L");
  assert.equal(api.model.entries.some((e) => e.name === "i_L"), false);
});

test("look: off-grid raw primitive coordinates are sanitized onto the 5-unit grid", () => {
  const raw: IconDef = {
    w: 1,
    h: 1,
    g: [
      { t: "line", p: [[3, 48], [97, 52]] as Prim & { t: "line" } extends { p: infer P } ? P : never },
      { t: "rect", x: 12, y: 18, w: 63, h: 41, corner: "sm" },
    ],
  };
  const clean = sanitizeIconDef(raw);
  assert.equal(iconValid(clean), true);
  assert.deepEqual(clean.g[0], { t: "line", p: [[5, 50], [95, 50]] });
  assert.deepEqual(clean.g[1], { t: "rect", x: 10, y: 20, w: 65, h: 40, corner: "sm" });
});
