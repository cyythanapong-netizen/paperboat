import type { RunSettings } from "./protocol";
import { NumberField } from "./NumberField";
import { changed } from "./project";

const SPEEDS = [0.25, 0.5, 1, 2, 4, 0];

/** The duration the ∞ key restores when clicked. */
const finite = (defaults: RunSettings) => (defaults.duration && defaults.duration > 0 ? defaults.duration : 10);

/**
 * How long and how fast the next run goes, beside Run. Starts at the backend's defaults
 * (hello.run); only what the user changed is sent with graph.run. The speed also changes a
 * run in progress (run.speed, #219), and shows the speed it runs at. Hidden for a backend
 * that evaluates once, or doesn't say.
 */
export function RunOptions({ defaults, value, onChange }: { defaults: RunSettings; value: RunSettings; onChange: (v: RunSettings) => void }) {
  if (defaults.mode !== "clock") return null;
  const duration = value.duration ?? defaults.duration ?? 10;
  const speed = value.speed ?? defaults.speed ?? 1;
  const set = (change: RunSettings) => onChange(changed({ ...value, ...change }, defaults));
  const isInfinite = duration === 0;

  return (
    <span className="run-options" title="The next run's length and speed (the backend's defaults until changed; 0 s runs forever)">
      for
      {/* One slot, one width: a well for the number, or the same well as a pressed key saying ∞. */}
      {isInfinite ? (
        <button
          type="button"
          className="key small run-duration on"
          aria-pressed="true"
          title="Infinite run (0 s): click to set a finite duration"
          onClick={() => set({ duration: finite(defaults) })}
        >
          ∞
        </button>
      ) : (
        <NumberField className="run-duration" value={duration} valid={(v) => v >= 0} onChange={(duration) => set({ duration })} />
      )}
      s
      <select value={speed} title="Times real time; during a run, it changes the run at once" onChange={(e) => set({ speed: Number(e.target.value) })}>
        {SPEEDS.map((x) => (
          <option key={x} value={x}>
            {x === 0 ? "max" : `${x}×`}
          </option>
        ))}
      </select>
    </span>
  );
}
