import assert from "node:assert/strict";
import { test } from "node:test";
import { initial, reduce } from "../src/engine.ts";

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
