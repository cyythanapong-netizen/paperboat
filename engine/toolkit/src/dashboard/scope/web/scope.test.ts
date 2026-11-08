import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { findTrigger, spectrum, stats, toCsv, valueAt } from "./math.ts";

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

test("the web folder imports nothing from the app but types, so a toolkit can be dropped in or taken out", () => {
  const dir = path.dirname(fileURLToPath(import.meta.url));
  const files = fs.readdirSync(dir, { recursive: true, encoding: "utf8" }).filter((f) => /\.(tsx?|css)$/.test(f) && !f.endsWith(".test.ts"));
  for (const file of files) {
    const src = fs.readFileSync(path.join(dir, file), "utf8");
    for (const m of src.matchAll(/^(import|export)\s+(type\s+)?[^;]*?from\s+"([^"]+)"/gms)) {
      const [, , type, from] = m;
      const ok = from.startsWith("./") || ["react", "react/jsx-runtime", "uplot"].includes(from) || from.startsWith("uplot/") || (from === "@paperboat/host" && type);
      assert.ok(ok, `${file} imports ${from}`);
    }
  }
});
