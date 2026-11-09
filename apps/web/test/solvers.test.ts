import assert from "node:assert/strict";
import { test } from "node:test";
import { availableSolvers, SOLVERS } from "../src/solvers.ts";

test("a backend that does not announce its solvers offers all of them", () => {
  assert.deepEqual(availableSolvers({}), SOLVERS);
});

test("the UI offers only the solvers the engine announced (#217)", () => {
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "rk45"] }), ["rk4", "rk45"]);
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "cvode", "ida"] }), ["rk4", "cvode", "ida"]);
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "mystery"] }), ["rk4"]);
  assert.deepEqual(availableSolvers({ solvers: [] }), []);
});
