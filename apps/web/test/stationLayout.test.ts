import assert from "node:assert/strict";
import { test } from "node:test";
import { arrangeStationObjects } from "../src/stationLayout.ts";
import type { Station } from "../src/protocol.ts";

const sizes = [
  { id: "base", width: 180, height: 90 },
  { id: "arm", width: 140, height: 120 },
  { id: "tool", width: 200, height: 70 },
];

const linked: Station = {
  objects: [
    { id: "tool", type: "gripper", props: { width: 0.2 }, links: { mount: "arm" } },
    { id: "base", type: "base", props: { mass: 10 } },
    { id: "arm", type: "arm", links: { parent: "base" } },
  ],
};
const links = [
  { source: "arm", target: "tool" },
  { source: "base", target: "arm" },
];

test("station objects follow their links and every other field survives", () => {
  const before = structuredClone(linked);
  const next = arrangeStationObjects(linked, sizes, links);
  const at = new Map(next.objects.map((o) => [o.id, o]));
  assert.ok(at.get("base")!.pos![0] < at.get("arm")!.pos![0]);
  assert.ok(at.get("arm")!.pos![0] < at.get("tool")!.pos![0]);
  assert.deepEqual(next.objects.map((o) => ({ ...o, pos: undefined })), before.objects.map((o) => ({ ...o, pos: undefined })));
  assert.deepEqual(linked, before, "the input station was not mutated");
});

test("station layout is deterministic and an already arranged station is a no-op", () => {
  const once = arrangeStationObjects(linked, sizes, links);
  const twice = arrangeStationObjects(once, sizes, links);
  assert.equal(twice, once);
  const shuffled = arrangeStationObjects(linked, [...sizes].reverse(), [...links].reverse());
  assert.deepEqual(shuffled, once);
});

test("empty stations and already positioned isolated objects do not change", () => {
  const empty: Station = { objects: [] };
  assert.equal(arrangeStationObjects(empty, [], []), empty);
  const one: Station = { objects: [{ id: "base", type: "base", pos: [0, 0], links: { self: "base" } }] };
  assert.equal(arrangeStationObjects(one, [{ id: "base" }], [{ source: "base", target: "base" }]), one);
});
