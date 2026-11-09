import assert from "node:assert/strict";
import { test } from "node:test";
import { directionName, directionOf, directionTitle, keepAttr, typeFromClasses, writeAttr } from "../src/portDirection.ts";

test("a target handle takes a wire in, a source sends one out", () => {
  assert.equal(directionOf("target"), "in");
  assert.equal(directionOf("source"), "out");
});

test("the port's type comes from a handle's t- class or a row's port- class, not layout classes", () => {
  assert.equal(typeFromClasses(["react-flow__handle", "target", "t-numbers"]), "numbers");
  assert.equal(typeFromClasses(["port", "in", "side-left", "port-signal", "nodrag"]), "signal");
  assert.equal(typeFromClasses(["port-row"]), undefined);
  assert.equal(typeFromClasses(["port-strip", "top"]), undefined);
  assert.equal(typeFromClasses(["port-tip", "in"]), undefined);
  // A compact plate's handle of a port with no type says `t-undefined`.
  assert.equal(typeFromClasses(["t-undefined"]), undefined);
  assert.equal(typeFromClasses([]), undefined);
});

test("the accessible name leads with the direction, then the label and the type", () => {
  assert.equal(directionName("in", "in", "signal"), "input in, signal");
  assert.equal(directionName("out", "y (m) [3]", "numbers"), "output y (m) [3], numbers");
  assert.equal(directionName("out", "next"), "output next");
});

test("the hover title says the direction and type, and keeps what was there after it", () => {
  assert.equal(directionTitle("in", "signal", undefined), "Input (signal)");
  assert.equal(directionTitle("out", undefined, ""), "Output");
  assert.equal(directionTitle("in", "signal", "Unit: m"), "Input (signal) · Unit: m");
  assert.equal(directionTitle("in", "step", "A step port only takes a step wire"), "Input (step) · A step port only takes a step wire");
});

/** An element's attributes, as React and the observer see them. */
function element(attrs: Record<string, string> = {}) {
  const map = new Map(Object.entries(attrs));
  return {
    map,
    getAttribute: (n: string) => map.get(n) ?? null,
    setAttribute: (n: string, v: string) => void map.set(n, v),
  };
}

test("the words are added once, and what React writes later is kept after them", () => {
  const h = element({ title: "Unit: m" });
  const annotate = () => writeAttr(h, "title", directionTitle("in", "signal", keepAttr(h, "title")));
  annotate();
  assert.equal(h.map.get("title"), "Input (signal) · Unit: m");
  // The observer sees its own write: nothing changes, and nothing is written again.
  const before = new Map(h.map);
  annotate();
  assert.deepEqual(h.map, before);
  // While a wire is dragged React writes why it won't fit, then puts its own title back.
  h.map.set("title", "Can't take a step wire");
  annotate();
  assert.equal(h.map.get("title"), "Input (signal) · Can't take a step wire");
  h.map.delete("title");
  annotate();
  assert.equal(h.map.get("title"), "Input (signal)");
});

test("a dot with no name of its own is named after its handle", () => {
  const h = element();
  assert.equal(keepAttr(h, "aria-label"), "");
  writeAttr(h, "aria-label", directionName("out", keepAttr(h, "aria-label") || "y", "signal"));
  assert.equal(h.map.get("aria-label"), "output y, signal");
  assert.equal(keepAttr(h, "aria-label"), "");
});
