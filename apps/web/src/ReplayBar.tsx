// The replay controls (#284), on the run dock: step back, play or pause, step forward, a timeline
// scrubber, the playback speed and the way back to live. It only appears when there is a run to
// scrub through (or while one is open), so a backend that never samples keeps the dock as it was.

import type { EngineState } from "./engine";
import { REPLAY_SPEEDS } from "./replay";

type ReplayApi = {
  start: () => void;
  exit: () => void;
  seek: (t: number) => void;
  step: (direction: 1 | -1) => void;
  play: (playing: boolean) => void;
  speed: (speed: number) => void;
};

const time = (t: number) => `${t.toFixed(2)} s`;

/** The dock's replay key (idle) or transport bar (open). Nothing while a run is in progress. */
export function ReplayBar({ state, replay }: { state: EngineState; replay: ReplayApi }) {
  if (state.run?.status === "running") return null;
  const r = state.replay;
  if (!r) {
    if (!state.traces.span()) return null;
    return (
      <button className="key small replay-open" title="Scrub through the last run (its scopes, wires and Station)" onClick={replay.start}>
        ⏱ Replay
      </button>
    );
  }
  const span = r.to - r.from;
  const step = Math.max(span / 500, 1e-4);
  return (
    <span className="replay-bar" role="group" aria-label="Replay">
      <button className="key small replay-step" title="Step back by the run's sample interval" onClick={() => replay.step(-1)}>
        ◀
      </button>
      <button className="key small replay-play" title={r.playing ? "Pause" : "Play"} aria-pressed={r.playing} onClick={() => replay.play(!r.playing)}>
        {r.playing ? "❚❚" : "▶"}
      </button>
      <button className="key small replay-step" title="Step forward by the run's sample interval" onClick={() => replay.step(1)}>
        ▶
      </button>
      <input
        className="replay-slider"
        type="range"
        aria-label="Timeline"
        title="Jump to any point in the run"
        min={r.from}
        max={r.to}
        step={step}
        value={r.time}
        onChange={(e) => replay.seek(Number(e.target.value))}
      />
      <output className="replay-time" title="The sim time the scopes, wires and Station show">
        {time(r.time)}
      </output>
      <select className="replay-speed" aria-label="Playback speed" title="How fast the replay plays" value={r.speed} onChange={(e) => replay.speed(Number(e.target.value))}>
        {REPLAY_SPEEDS.map((x) => (
          <option key={x} value={x}>
            {x}×
          </option>
        ))}
      </select>
      <button className="key small replay-exit" title="Leave replay and follow the next run" onClick={replay.exit}>
        Live
      </button>
    </span>
  );
}
