import assert from "node:assert/strict";
import { test } from "node:test";
import { initial, missingRequiredToolkits, reduce } from "../src/engine.ts";

test("console starts ready before the first run and records non-run logs", () => {
  assert.equal(initial.run, null);
  assert.deepEqual(initial.logs, []);

  // Backend connection before any run logs to the Console.
  const s1 = reduce(initial, { type: "hello", protocol: "paperboat/0", engine: "cart-pole" });
  assert.equal(s1.run, null);
  assert.deepEqual(s1.logs, [{ level: "info", text: "connected to cart-pole" }]);

  // Non-run action/error logs are appended while no run has started.
  const s2 = reduce(s1, { type: "log", level: "error", text: "invalid variables: x is missing" });
  assert.equal(s2.run, null);
  assert.equal(s2.logs.length, 2);
  assert.deepEqual(s2.logs[1], { level: "error", text: "invalid variables: x is missing", node: undefined });

  // Starting a run later preserves the logs recorded before the first run.
  const s3 = reduce(s2, { type: "run.started", run: "run-1" });
  assert.equal(s3.logs.length, 2);
});

test("console records connection failures before and after connecting without flooding retries", () => {
  const s1 = reduce(initial, { type: "connection", value: "closed" });
  assert.deepEqual(s1.logs, [{ level: "warn", text: "waiting for backend, reconnecting…" }]);

  // Repeated retry cycles do not duplicate the waiting message.
  const s2 = reduce(reduce(s1, { type: "connection", value: "connecting" }), { type: "connection", value: "closed" });
  assert.equal(s2.logs.length, 1);

  // Preserves logs across reset (e.g. switching or reconnecting backend).
  const s3 = reduce(s2, { type: "reset" });
  assert.equal(s3.logs.length, 1);
});

test("toolkits status updates from hello and toolkits.status and detects missing required toolkits", () => {
  const s1 = reduce(initial, {
    type: "hello",
    protocol: "paperboat/0",
    engine: "parts",
    toolkits: [{ name: "opencv", repo: "https://example.com/opencv.git", installed: false, required: true }],
  });
  assert.equal(s1.toolkits.length, 1);
  assert.equal(s1.toolkits[0].installed, false);
  assert.deepEqual(missingRequiredToolkits(s1.toolkits).map((t) => t.name), ["opencv"]);

  const s2 = reduce(s1, {
    type: "toolkits.status",
    toolkits: [
      {
        name: "opencv",
        repo: "https://example.com/opencv.git",
        installed: true,
        required: true,
        version: "0.1.0",
        versions: ["0.1.0", "0.0.9"],
        default: "0.1.0",
      },
    ],
  });
  assert.equal(s2.toolkits[0].installed, true);
  assert.equal(s2.toolkits[0].version, "0.1.0");
  assert.deepEqual(s2.toolkits[0].versions, ["0.1.0", "0.0.9"]);
  assert.equal(s2.toolkits[0].default, "0.1.0");
  assert.deepEqual(missingRequiredToolkits(s2.toolkits), []);

  // Also detects missing toolkits from missing block types in an opened project/flow.
  assert.deepEqual(
    missingRequiredToolkits([{ name: "audio", repo: "https://example.com/audio.git", installed: false, blocks: ["oscillator"] }], ["oscillator"]).map((t) => t.name),
    ["audio"],
  );
  // A toolkit that does not list the missing type is not blamed for it.
  assert.deepEqual(missingRequiredToolkits([{ name: "audio", repo: "https://example.com/audio.git", installed: false }], ["oscillator"]), []);
});

test("reducer ignores unknown message types without crashing or dropping state", () => {
  const s1 = reduce(initial, { type: "hello", protocol: "paperboat/0", engine: "cartpole" });
  assert.equal(s1.engine, "cartpole");
  const s2 = reduce(s1, { type: "toolkits", available: [], installed: [] } as any);
  assert.ok(s2);
  assert.equal(s2.engine, "cartpole");
  assert.equal(s2.logs.at(-1)?.text, 'ignored unknown message type "toolkits"');
  assert.equal(s2.logs.at(-1)?.level, "warn");
});

test("run.speed (#219): the running run takes the speed and where it was; another run's, or none running, changes nothing", () => {
  const started = reduce(initial, { type: "run.started", run: "run-1" });
  const going = reduce(reduce(started, { type: "run.progress", run: "run-1", fraction: 0.3 }), { type: "signal", node: "g", t: 3, v: 1 });
  const fast = reduce(going, { type: "run.speed", run: "run-1", speed: 4 });
  assert.equal(fast.run?.speed, 4);
  assert.deepEqual({ ...fast.run?.pacedFrom, at: 0 }, { at: 0, simTime: 3, progress: 0.3 });
  assert.equal(fast.logs.length, going.logs.length); // not an unknown message
  assert.equal(reduce(going, { type: "run.speed", run: "run-0", speed: 4 }), going);
  const done = reduce(fast, { type: "run.finished", run: "run-1", status: "done" });
  assert.equal(reduce(done, { type: "run.speed", run: "run-1", speed: 1 }), done);
  assert.equal(reduce(initial, { type: "run.speed", speed: 1 }), initial);
});
