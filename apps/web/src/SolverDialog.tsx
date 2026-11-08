import { useRef } from "react";
import type { RunSettings } from "./protocol";
import { NumberField } from "./NumberField";
import { useFocusReturn } from "./ProjectUI";
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

const FIELDS: { key: keyof SolverOptions; label: string; hint: string; whole?: boolean; adaptive: boolean; valid: (v: number) => boolean }[] = [
  { key: "rtol", label: "Relative tolerance", hint: "rtol, per component", adaptive: true, valid: (v) => v > 0 },
  { key: "atol", label: "Absolute tolerance", hint: "atol, per component", adaptive: true, valid: (v) => v >= 0 },
  { key: "min_step", label: "Min step, s", hint: "rk45 gives up below it (0: automatic)", adaptive: true, valid: (v) => v >= 0 },
  { key: "max_step", label: "Max step, s", hint: "0: the clock step", adaptive: true, valid: (v) => v >= 0 },
  { key: "max_steps", label: "Max sub-steps", hint: "per clock step, before it gives up", whole: true, adaptive: true, valid: (v) => v >= 1 },
  { key: "sub_steps", label: "Sub-steps per clock step", hint: "steps of dt / n", whole: true, adaptive: false, valid: (v) => v >= 1 && v <= 10000 },
];

/**
 * Run > Solver settings… (#196): the run's solver and its options, used by every block and
 * station object whose own solver is auto. Changes apply to the next run and save with the
 * project; only what differs from the backend's defaults (hello.run) is kept.
 */
export function SolverDialog({ defaults, value, onChange, onClose }: { defaults: RunSettings; value: RunSettings; onChange: (v: RunSettings) => void; onClose: () => void }) {
  const root = useRef<HTMLElement>(null);
  useFocusReturn(root);
  const solver = (value.solver ?? defaults.solver ?? "rk4") as Solver;
  const base = { ...ENGINE, ...defaults.solver_options };
  const options = { ...base, ...value.solver_options };
  const set = (change: RunSettings) => onChange(changed({ ...value, ...change }, defaults));
  const setOption = (key: keyof SolverOptions, v: number) => set({ solver_options: { ...value.solver_options, [key]: v } });
  const fields = FIELDS.filter((f) => (solver === "rk45" ? f.adaptive : solver !== "discrete" && !f.adaptive));

  return (
    <section
      className="console-window notice solver-dialog"
      role="dialog"
      aria-label="Solver settings"
      data-own-keys
      ref={root}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          onClose();
        }
      }}
      style={{ maxWidth: 440 }}
    >
      <header className="console-head">
        <h2>Solver settings</h2>
        <span className="spacer" />
        <button className="key small" onClick={onClose} aria-label="Close">✕</button>
      </header>
      <div className="notice-body" style={{ padding: "16px 20px", display: "grid", gap: 10 }}>
        <p style={{ margin: 0, color: "var(--text-muted)" }}>
          The run's solver: every block and station object whose own solver is <b>auto</b> uses it. Applies to the next run.
        </p>
        <label style={{ display: "grid", gridTemplateColumns: "1fr 1fr", alignItems: "center", gap: 8 }}>
          Solver
          <select aria-label="Solver" value={solver} onChange={(e) => set({ solver: e.target.value as Solver })}>
            {SOLVERS.map((s) => (
              <option key={s} value={s}>
                {SOLVER_LABELS[s]}
              </option>
            ))}
          </select>
        </label>
        {solver === "discrete" && (
          <p style={{ margin: 0, color: "var(--text-muted)" }}>A difference equation steps once per clock step: no options.</p>
        )}
        {fields.map((f) => (
          <label key={f.key} title={f.hint} style={{ display: "grid", gridTemplateColumns: "1fr 1fr", alignItems: "center", gap: 8 }}>
            {f.label}
            <NumberField
              className="num"
              aria-label={f.label}
              value={options[f.key]}
              valid={(v) => f.valid(v) && (!f.whole || Number.isInteger(v))}
              onChange={(v) => setOption(f.key, v)}
            />
          </label>
        ))}
        <div style={{ display: "flex", gap: 8, justifyContent: "flex-end" }}>
          <button type="button" className="key" onClick={() => onChange(changed({ ...value, solver: undefined, solver_options: undefined }, defaults))}>
            Backend's defaults
          </button>
          <button type="button" className="key primary" onClick={onClose}>
            Done
          </button>
        </div>
      </div>
    </section>
  );
}
