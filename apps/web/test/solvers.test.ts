import assert from "node:assert/strict";
import { test } from "node:test";
import { availableSolvers, settingsForScope, SOLVERS } from "../src/solvers.ts";

test("a backend that does not announce its solvers offers all of them", () => {
  assert.deepEqual(availableSolvers({}), SOLVERS);
});

test("the UI offers only the solvers the engine announced (#217)", () => {
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "rk45"] }), ["rk4", "rk45"]);
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "cvode", "ida"] }), ["rk4", "cvode", "ida"]);
  assert.deepEqual(availableSolvers({ solvers: ["rk4", "mystery"] }), ["rk4"]);
  assert.deepEqual(availableSolvers({ solvers: [] }), []);
});

test("a run with a Scope follows the smallest solver step; a run without one keeps its clock", () => {
  const defaults = { dt: 0.5, solver_options: { sub_steps: 1 } };
  assert.deepEqual(settingsForScope({}, defaults, false), {});
  // A max step below the clock becomes the clock: the Scope gets that resolution.
  assert.deepEqual(settingsForScope({ solver_options: { max_step: 0.0001 } }, defaults, true), {
    dt: 0.0001,
    solver_options: { max_step: 0.0001 },
  });
  // Fixed sub-steps become the clock, and the solver does not sub-step them again.
  assert.deepEqual(settingsForScope({ solver_options: { sub_steps: 4 } }, defaults, true), {
    dt: 0.125,
    solver_options: { sub_steps: 1 },
  });
  // The smallest wins, and a clock already finer than the solver stays.
  assert.deepEqual(settingsForScope({ dt: 0.2, solver_options: { max_step: 0.01, sub_steps: 4 } }, defaults, true), {
    dt: 0.01,
    solver_options: { max_step: 0.01, sub_steps: 1 },
  });
  assert.deepEqual(settingsForScope({ dt: 0.001, solver_options: { max_step: 0.01 } }, defaults, true), {
    dt: 0.001,
    solver_options: { max_step: 0.01 },
  });
});
