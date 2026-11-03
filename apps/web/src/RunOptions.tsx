import type { RunSettings } from "./protocol";

const SPEEDS = [0.25, 0.5, 1, 2, 4, 0];

/**
 * How long and how fast the next run goes, beside Run. Starts at the backend's defaults
 * (hello.run); only what the user changed is sent with graph.run. Hidden for a backend
 * that evaluates once, or doesn't say.
 */
export function RunOptions({ defaults, value, onChange }: { defaults: RunSettings; value: RunSettings; onChange: (v: RunSettings) => void }) {
  if (defaults.mode !== "clock") return null;
  const duration = value.duration ?? defaults.duration ?? 10;
  const speed = value.speed ?? defaults.speed ?? 1;
  const set = (change: RunSettings) => onChange(changed({ ...value, ...change }, defaults));
  return (
    <span className="run-options" title="The next run's length and speed (the backend's defaults until changed)">
      for
      <input
        type="number"
        min={0}
        step="any"
        value={duration}
        onChange={(e) => e.target.valueAsNumber > 0 && set({ duration: e.target.valueAsNumber })}
      />
      s
      <select value={speed} onChange={(e) => set({ speed: Number(e.target.value) })}>
        {SPEEDS.map((x) => (
          <option key={x} value={x}>
            {x === 0 ? "max" : `${x}×`}
          </option>
        ))}
      </select>
    </span>
  );
}

/** Only the settings that differ from the backend's defaults. */
export function changed(value: RunSettings, defaults: RunSettings): RunSettings {
  return Object.fromEntries(
    Object.entries(value).filter(([k, v]) => v !== undefined && v !== defaults[k as keyof RunSettings]),
  ) as RunSettings;
}
