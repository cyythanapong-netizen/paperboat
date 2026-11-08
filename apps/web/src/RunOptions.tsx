import type { RunSettings } from "./protocol";
import { NumberField } from "./NumberField";

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
  const isInfinite = duration === 0;

  return (
    <span className="run-options" title="The next run's length and speed (the backend's defaults until changed)">
      for
      {isInfinite ? (
        <span
          className="num infinity-tag"
          title="Infinite run (0 s): click to set a finite duration"
          onClick={() => set({ duration: defaults.duration && defaults.duration > 0 ? defaults.duration : 10 })}
          style={{ cursor: "pointer", padding: "0 4px", userSelect: "none" }}
        >
          ∞
        </span>
      ) : (
        <NumberField className="num" value={duration} valid={(v) => v >= 0} onChange={(duration) => set({ duration })} />
      )}
      s
      <button
        type="button"
        className={`infinity-btn ${isInfinite ? "active" : ""}`}
        title={isInfinite ? "Infinite run active (0 s); click to restore finite duration" : "Set infinite run duration (0 s)"}
        style={{
          border: "none",
          background: isInfinite ? "var(--signal)" : "transparent",
          color: isInfinite ? "var(--text-on-signal)" : "inherit",
          borderRadius: 3,
          cursor: "pointer",
          padding: "1px 5px",
          marginLeft: 2,
          fontWeight: "bold",
          fontSize: 13,
          lineHeight: 1,
        }}
        onClick={() => set({ duration: isInfinite ? (defaults.duration && defaults.duration > 0 ? defaults.duration : 10) : 0 })}
      >
        ∞
      </button>
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
