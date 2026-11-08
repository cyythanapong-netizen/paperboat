import { SolverSettings, type SolverInfo, type SolverOptionInfo } from "@paperboat/shared/components";
import type { RunSettings } from "./protocol";
import { changed } from "./project";

type SolverOptions = NonNullable<RunSettings["solver_options"]>;
type Solver = NonNullable<RunSettings["solver"]>;

export const SOLVERS: Solver[] = ["rk4", "rk45", "rk2", "euler", "discrete"];
export const SOLVER_LABELS: Record<Solver, string> = {
  rk4: "RK4 (Classic)",
  rk45: "RK45 (Adaptive)",
  rk2: "RK2 (Heun)",
  euler: "Euler",
  discrete: "Discrete",
};

// The engine's own defaults (solve::Options), for a backend that doesn't announce them.
const ENGINE: Required<SolverOptions> = { rtol: 1e-6, atol: 1e-9, min_step: 0, max_step: 0, max_steps: 100000, sub_steps: 1 };

const ADAPTIVE = ["rtol", "atol", "max_step", "min_step", "max_steps"];
const INFO: SolverInfo[] = [
  { id: "rk45", label: SOLVER_LABELS.rk45, family: "adaptive", blurb: "Dormand–Prince 4(5): steps to meet the tolerances", options: ADAPTIVE },
  { id: "rk4", label: SOLVER_LABELS.rk4, family: "explicit", blurb: "Fourth order, fixed sub-steps per clock step", options: ["sub_steps"], recommended: true },
  { id: "rk2", label: SOLVER_LABELS.rk2, family: "explicit", blurb: "Second order, cheaper and rougher", options: ["sub_steps"] },
  { id: "euler", label: SOLVER_LABELS.euler, family: "explicit", blurb: "First order; gains energy on oscillators", options: ["sub_steps"] },
  { id: "discrete", label: SOLVER_LABELS.discrete, family: "discrete", blurb: "A difference equation per clock step", options: [] },
];

const OPTIONS: SolverOptionInfo[] = [
  { key: "rtol", label: "Relative tolerance", hint: "Error allowed per step, relative to each value.", group: "Accuracy", valid: (v) => v > 0 },
  { key: "atol", label: "Absolute tolerance", hint: "Error allowed near zero.", group: "Accuracy", valid: (v) => v >= 0 },
  { key: "max_step", label: "Max step", unit: "s", hint: "Largest internal step.", zero: "the clock step", group: "Steps", valid: (v) => v >= 0 },
  { key: "min_step", label: "Min step", unit: "s", hint: "It gives up below this.", zero: "automatic", group: "Steps", valid: (v) => v >= 0 },
  { key: "max_steps", label: "Max sub-steps", hint: "Per clock step, before it gives up.", group: "Steps", whole: true, valid: (v) => v >= 1 },
  { key: "sub_steps", label: "Sub-steps per clock step", hint: "Steps of dt / n: more is closer and slower.", group: "Steps", whole: true, valid: (v) => v >= 1 && v <= 10000 },
];

/**
 * Run > Solver settings… (#196): the run's solver and its options, used by every block and
 * station object whose own solver is auto. Changes apply to the next run and save with the
 * project; only what differs from the backend's defaults (hello.run) is kept. The dialog is the
 * kit's SolverSettings, shared with Squero Studio.
 */
export function SolverDialog({ defaults, value, onChange, onClose }: { defaults: RunSettings; value: RunSettings; onChange: (v: RunSettings) => void; onClose: () => void }) {
  const solver = (value.solver ?? defaults.solver ?? "rk4") as Solver;
  const set = (change: RunSettings) => onChange(changed({ ...value, ...change }, defaults));
  return (
    <SolverSettings
      intro="The run's solver: every block and station object whose own solver is auto uses it. Applies to the next run. A Modelica FMU object keeps the solver it was exported with."
      solvers={INFO}
      options={OPTIONS}
      solver={solver}
      values={(value.solver_options ?? {}) as Record<string, number | undefined>}
      defaults={{ ...ENGINE, ...defaults.solver_options }}
      onSolver={(id) => set({ solver: id as Solver })}
      onOption={(key, v) => {
        const next = { ...value.solver_options, [key]: v } as Record<string, number | undefined>;
        if (v === undefined) delete next[key];
        set({ solver_options: next as SolverOptions });
      }}
      onReset={() => onChange(changed({ ...value, solver: undefined, solver_options: undefined }, defaults))}
      onClose={onClose}
    />
  );
}
