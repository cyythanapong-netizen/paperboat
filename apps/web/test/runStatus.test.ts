import assert from "node:assert/strict";
import { test } from "node:test";
import { dockSettings, runBlocker, runClock, speedChange } from "../src/runStatus.ts";

test("why Run is off, first reason first", () => {
  assert.equal(runBlocker(true, "closed", true, true), "A run is in progress");
  assert.equal(runBlocker(false, "connecting", true, true), "Not connected to a backend");
  assert.equal(runBlocker(false, "open", true, true), "Required toolkits are not installed");
  assert.equal(runBlocker(false, "open", false, true), "Flow contains blocks with missing types");
  assert.equal(runBlocker(false, "open", false, false), "");
});

test("no run: nothing to show", () => {
  assert.deepEqual(runClock(null, {}, { duration: 10 }, 0), { elapsed: "", delaySeconds: 0, isDelayed: false });
});

test("a run of a set length shows how far it is", () => {
  const run = { id: "r", progress: 0.25, status: "running" as const };
  assert.equal(runClock(run, {}, { duration: 10 }, 0).elapsed, "2.5 / 10 s");
  assert.equal(runClock(run, { duration: 20 }, { duration: 10 }, 0).elapsed, "5.0 / 20 s"); // the user's setting wins
});

test("an endless run shows its sim time", () => {
  const run = { id: "r", progress: 0, status: "running" as const, simTime: 3.21 };
  assert.equal(runClock(run, { duration: 0 }, {}, 0).elapsed, "3.2 s");
});

test("real-time pacing that falls behind the wall clock is a delay", () => {
  // 5 s of wall clock for 2 s of sim time at 1×: 3 s behind.
  const run = { id: "r", progress: 0.2, status: "running" as const, startedAt: 1000, simTime: 2 };
  const late = runClock(run, {}, { duration: 10, speed: 1 }, 6000);
  assert.equal(late.delaySeconds, 3);
  assert.equal(late.isDelayed, true);
  // On time, or too soon after the start to tell: none.
  assert.equal(runClock({ ...run, simTime: 5 }, {}, { duration: 10, speed: 1 }, 6000).isDelayed, false);
  assert.equal(runClock(run, {}, { duration: 10, speed: 1 }, 2000).delaySeconds, 0);
});

test("a speed change during a run (#219) is sent; between runs, or with the speed unchanged, nothing is", () => {
  const defaults = { duration: 10, speed: 1 };
  assert.deepEqual(speedChange(true, {}, { speed: 0 }, defaults), { type: "run.speed", speed: 0 });
  assert.deepEqual(speedChange(true, { speed: 4 }, {}, defaults), { type: "run.speed", speed: 1 }); // back to the default
  assert.equal(speedChange(false, {}, { speed: 4 }, defaults), null); // the next graph.run brings it
  assert.equal(speedChange(true, {}, { speed: 1 }, defaults), null); // the default, said out loud: no change
  assert.equal(speedChange(true, { speed: 2 }, { speed: 2, duration: 20 }, defaults), null); // the length waits for the next run
});

test("the dock shows the speed the run took, while it runs", () => {
  const run = { id: "r", progress: 0.1, status: "running" as const, speed: 4 };
  assert.deepEqual(dockSettings(run, { duration: 20, speed: 2 }), { duration: 20, speed: 4 });
  assert.deepEqual(dockSettings({ ...run, status: "done" }, { speed: 2 }), { speed: 2 }); // after it: the user's choice
  assert.deepEqual(dockSettings({ ...run, speed: undefined }, { speed: 2 }), { speed: 2 }); // none taken yet
  assert.deepEqual(dockSettings(null, {}), {});
});

test("after a speed change the delay counts from where the run took it, at its new speed", () => {
  // 2 s of sim time in 2 s at 1×, then 4× from there: 2 s of wall clock later the run is at 10 s.
  const at4 = { id: "r", progress: 1, status: "running" as const, startedAt: 0, simTime: 10, speed: 4, pacedFrom: { at: 2000, simTime: 2, progress: 0.2 } };
  assert.equal(runClock(at4, { speed: 4 }, { duration: 10, speed: 1 }, 4000).isDelayed, false);
  // A real delay still shows: 4 s of sim time at 4× is 1 s of wall clock, and 2 s went by.
  const late = runClock({ ...at4, simTime: 6 }, { speed: 4 }, { duration: 10, speed: 1 }, 4000);
  assert.equal(late.delaySeconds, 1.0);
  assert.equal(late.isDelayed, true);
  // Slowed down: 4× for 1 s (4 s of sim time), then 1× for 3 s more: on time at 7 s, though from the start at 1× it is 3 s "ahead".
  const at1 = { id: "r", progress: 0.7, status: "running" as const, startedAt: 0, simTime: 7, speed: 1, pacedFrom: { at: 1000, simTime: 4, progress: 0.4 } };
  assert.equal(runClock(at1, { speed: 1 }, { duration: 10, speed: 4 }, 4000).delaySeconds, 0);
  // Sped up from 1× to max: no pacing, no delay.
  assert.equal(runClock({ ...at1, speed: 0 }, { speed: 0 }, { duration: 10 }, 9000).isDelayed, false);
  // Too soon after the change to tell.
  assert.equal(runClock({ ...at4, simTime: 2.1 }, { speed: 4 }, { duration: 10 }, 2500).delaySeconds, 0);
});
