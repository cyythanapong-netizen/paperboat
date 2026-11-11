import type { RunSettings } from "./protocol";

export type Solver = NonNullable<RunSettings["solver"]>;

/** Every solver the app knows, in menu order. */
export const SOLVERS: Solver[] = ["rk4", "rk45", "rk2", "euler", "discrete", "cvode", "ida"];

export const SOLVER_LABELS: Record<Solver, string> = {
  rk4: "RK4 (Classic)",
  rk45: "RK45 (Adaptive)",
  rk2: "RK2 (Heun)",
  euler: "Euler",
  discrete: "Discrete",
  cvode: "CVODE (BDF)",
  ida: "IDA (BDF)",
};

/** A solver's short label, as a menu names it: "RK4 (Classic)" → "RK4". */
export const solverShort = (s: Solver) => SOLVER_LABELS[s].replace(/ \(.*\)$/, "");

/** The solvers the backend announced in hello.run (all of them when it said nothing). */
export function availableSolvers(defaults: RunSettings): Solver[] {
  const listed = defaults.solvers;
  return listed ? SOLVERS.filter((s) => listed.includes(s)) : SOLVERS;
}

/**
 * A Scope is an instrument, not a glance: when one is on the Board, run the graph at the
 * smallest solver step so its samples are the solver's, not a coarse clock. The Graph still
 * draws one frame every few steps, while every pass reaches the Scope (and its CSV/PNG).
 * Fixed sub-steps become the clock itself (`sub_steps = 1`) so they are not applied twice.
 */
export function settingsForScope(value: RunSettings, defaults: RunSettings, hasScope: boolean): RunSettings {
  if (!hasScope) return value;
  const base = value.dt ?? defaults.dt ?? 0.01;
  const options = { ...value.solver_options };
  let dt = base;
  if (options.max_step !== undefined && options.max_step > 0) dt = Math.min(dt, options.max_step);
  if (options.sub_steps !== undefined && options.sub_steps > 1) {
    dt = Math.min(dt, base / options.sub_steps);
    options.sub_steps = 1;
  }
  return dt < base ? { ...value, dt, solver_options: options } : value;
}
