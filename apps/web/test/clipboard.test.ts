import assert from "node:assert/strict";
import { test } from "node:test";
import { copySelection, pasteClip } from "../src/clipboard.ts";

const block = (id: string, type: string, x: number, selected: boolean, extra: object = {}) =>
  ({ id, type: "block", position: { x, y: 0 }, selected, data: { def: { type, params: [] }, params: {} }, ...extra }) as never;
const wire = (s: string, t: string) => ({ id: `${s}.out-${t}.in`, type: "wire", source: s, sourceHandle: "out", target: t, targetHandle: "in", data: { type: "signal" } }) as never;

const nodes = [block("a-1", "a", 0, true), block("b-1", "b", 100, true), block("c-1", "c", 200, false), block("l-1", "l", 300, true, { data: { def: { type: "l", linked: true }, params: {} } })];
const edges = [wire("a-1", "b-1"), wire("b-1", "c-1"), wire("c-1", "a-1")];

test("copy keeps wires between selected blocks and trims loose ones", () => {
  const clip = copySelection(nodes, edges)!;
  assert.deepEqual(clip.nodes.map((n) => n.id), ["a-1", "b-1"]); // the linked block stays out
  assert.deepEqual(clip.edges.map((w) => w.id), ["a-1.out-b-1.in"]);
});

test("nothing selected, nothing copied", () => {
  assert.equal(copySelection(nodes.map((n) => ({ ...n, selected: false })), edges), null);
});

test("paste makes new ids, shifts, selects, and rewires", () => {
  const clip = copySelection(nodes, edges)!;
  const out = pasteClip(clip, nodes, edges, 30);
  assert.deepEqual(out.ids, ["a-2", "b-2"]);
  assert.deepEqual(out.nodes.filter((n) => n.selected).map((n) => n.id), ["a-2", "b-2"]);
  assert.equal(out.nodes.find((n) => n.id === "b-2")!.position.x, 130);
  const w = out.edges.at(-1)!;
  assert.deepEqual([w.id, w.source, w.target], ["a-2.out-b-2.in", "a-2", "b-2"]);
  assert.equal(out.edges.length, 4);
});

const frame = (id: string, kind: string, selected: boolean, x: number, data: object = {}, parentId?: string) =>
  ({ id, type: "frame", position: { x, y: 0 }, selected, ...(parentId ? { parentId } : {}), data: { kind, label: id, ...data } }) as never;

test("copying a group takes what is in it, nested groups too, at their places", () => {
  const ns = [
    frame("subsystem-1", "subsystem", true, 100),
    block("a-1", "a", 10, false, { parentId: "subsystem-1" }),
    frame("frame-1", "visual", false, 50, {}, "subsystem-1"),
    block("b-1", "b", 5, false, { parentId: "frame-1" }),
    block("c-1", "c", 400, false),
  ];
  const es = [wire("a-1", "b-1"), wire("b-1", "c-1")];
  const clip = copySelection(ns, es)!;
  assert.deepEqual(clip.nodes.map((n) => n.id), ["subsystem-1", "a-1", "frame-1", "b-1"]);
  assert.equal(clip.nodes[0].position.x, 100);
  assert.equal(clip.nodes[1].parentId, "subsystem-1");
  assert.deepEqual(clip.edges.map((w) => w.id), ["a-1.out-b-1.in"]);
});

test("pasting a group renames it and its insides, and the ports that name them", () => {
  const ns = [
    frame("subsystem-1", "subsystem", true, 100, { expose: [{ side: "in", to: ["a-1", "in"] }], release: [["a-1", "out"]] }),
    block("a-1", "a", 10, false, { parentId: "subsystem-1" }),
  ];
  const out = pasteClip(copySelection(ns, [])!, ns, [], 30);
  assert.deepEqual(out.ids, ["subsystem-2"]); // the block inside is not selected on its own
  const g = out.nodes.find((n) => n.id === "subsystem-2") as never as { position: { x: number }; data: { expose: { to: string[] }[]; release: string[][] } };
  assert.equal(g.position.x, 130);
  assert.deepEqual(g.data.expose[0].to, ["a-2", "in"]);
  assert.deepEqual(g.data.release, [["a-2", "out"]]);
  const inner = out.nodes.find((n) => n.id === "a-2")!;
  assert.equal(inner.parentId, "subsystem-2");
  assert.equal(inner.position.x, 10); // still its place in the group
});
