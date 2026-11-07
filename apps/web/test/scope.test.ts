import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { findTrigger, spectrum, stats, toCsv, valueAt } from "../src/scope/math.ts";
import { MAX_POINTS, TraceBuffer, TraceStore } from "../src/scope/trace.ts";

test("a trace buffer appends in O(1) and thins to keep the whole span", () => {
  const b = new TraceBuffer(8);
  for (let i = 0; i < 9; i++) b.push(i, i * 10);
  assert.ok(b.n <= 8);
  assert.equal(b.times[0], 0);
  assert.equal(b.times[b.n - 1], 8); // the latest is always kept
  assert.equal(b.last, 80);
});

test("10^6 samples append within a fixed budget", () => {
  const b = new TraceBuffer();
  const start = performance.now();
  for (let i = 0; i < 1e6; i++) b.push(i * 1e-3, Math.sin(i));
  const ms = performance.now() - start;
  assert.ok(b.n <= MAX_POINTS && b.times[b.n - 1] === 999.999);
  assert.ok(ms < 1000, `took ${ms.toFixed(0)} ms`);
});

test("the store makes one channel per input, and per component of a vector", () => {
  const s = new TraceStore();
  s.add({ node: "sc", port: "a", t: 0, v: 1 });
  s.add({ node: "sc", port: "c", t: 0, v: [1, 2], components: [{ id: "x", unit: "m" }, { id: "v" }] });
  s.add({ node: "g", t: 0, v: "text" });
  assert.deepEqual(s.channels("sc").map((c) => [c.id, c.label, c.unit]), [["a", "a", undefined], ["c.x", "c.x", "m"], ["c.v", "c.v", undefined]]);
  assert.equal(s.channels("g").length, 0);
  s.add({ node: "g", t: 0, v: [5, 6] }); // a Graph's vector: components by index
  assert.deepEqual(s.channels("g").map((c) => c.label), ["0", "1"]);
});

test("trigger, spectrum, stats, values and CSV", () => {
  const t = [0, 1, 2, 3, 4, 5], v = [0, 2, 0, 2, 0, 2];
  assert.equal(findTrigger(t, v, 1, "rising"), 4.5);
  assert.equal(findTrigger(t, v, 1, "rising", 3), 2.5);
  assert.equal(findTrigger(t, v, 1, "falling"), 3.5);
  assert.equal(findTrigger(t, [0, 0], 1), null);

  const dt = 1 / 64, n = 256;
  const sine = Array.from({ length: n }, (_, i) => Math.sin(2 * Math.PI * 8 * i * dt));
  const { f, db } = spectrum(sine, dt);
  const peak = db.indexOf(Math.max(...db));
  assert.equal(f[peak], 8);
  assert.ok(Math.abs(db[peak]) < 1.5, `peak ${db[peak]} dB`); // amplitude 1 reads about 0 dB

  const st = stats(t, v);
  assert.deepEqual([st.min, st.max, st.mean, st.n], [0, 2, 1, 6]);
  assert.equal(valueAt(t, v, 0.5), 1);
  assert.equal(valueAt(t, v, 9), null);
  assert.equal(toCsv([{ label: "a", unit: "m", t: [0, 1], v: [1, 2] }, { label: "b,c", t: [1], v: [3] }]), 't (s),a (m),"b,c"\n0,1,\n1,2,3\n');
});

test("scope/ imports nothing from the app, so it can move into a shared kit (#160)", () => {
  const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), "../src/scope");
  for (const file of fs.readdirSync(dir)) {
    if (file === "BoardScope.tsx") continue; // the adapter, which is the app's
    const src = fs.readFileSync(path.join(dir, file), "utf8");
    for (const m of src.matchAll(/from\s+"([^"]+)"/g)) assert.ok(m[1].startsWith("./") || ["react", "react/jsx-runtime", "uplot"].includes(m[1]) || m[1].startsWith("uplot/"), `${file} imports ${m[1]}`);
  }
});

test("a Scope's settings live in the Board layout and survive arrange → layoutOf", async () => {
  const { arrange, layoutOf, setScope } = await import("../src/dashboardLayout.ts");
  const monitors = { sc: { kind: "scope", x: 0, y: 0 }, g: { kind: "graph", x: 0, y: 100 } };
  const a = arrange(monitors, undefined, 6);
  assert.equal(a.instruments.find((p) => p.block === "sc")?.size, "large"); // a Scope opens large
  const saved = setScope(a, "sc", { mode: "xy", trigger: { source: "a", level: 0.5 } });
  const again = layoutOf(arrange(monitors, saved, 6));
  assert.deepEqual(again.instruments?.find((i) => i.block === "sc")?.scope, { mode: "xy", trigger: { source: "a", level: 0.5 } });
  assert.equal(layoutOf(arrange(monitors, setScope(arrange(monitors, saved, 6), "sc", {}), 6)).instruments?.find((i) => i.block === "sc")?.scope, undefined);
});
