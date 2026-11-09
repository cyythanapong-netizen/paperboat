import assert from "node:assert/strict";
import { test } from "node:test";
import { runBlocker, runClock } from "../src/runStatus.ts";

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
