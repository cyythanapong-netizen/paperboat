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
