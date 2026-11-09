// The Run dock's words: why a run can't start, and how far one has got against the wall clock.

import type { EngineState } from "./engine";
import type { ClientMsg, RunSettings } from "./protocol";

/** Why Run is off, in words; "" when it can run. */
export const runBlocker = (running: boolean, connection: EngineState["connection"], hasMissingToolkits: boolean, hasMissingBlocks: boolean) =>
  running
    ? "A run is in progress"
    : connection !== "open"
      ? "Not connected to a backend"
      : hasMissingToolkits
        ? "Required toolkits are not installed"
        : hasMissingBlocks
          ? "Flow contains blocks with missing types"
          : "";

/**
 * The run's time as the dock shows it (`elapsed`), and how far real-time pacing has fallen behind
 * the wall clock at `now` (ms).
 */
export function runClock(run: EngineState["run"], runSettings: RunSettings, runDefaults: RunSettings, now: number) {
  const durationVal = Number(runSettings.duration ?? runDefaults.duration);
  const isInfinite = durationVal <= 0;
  const simAt = (at: { simTime?: number; progress: number } | null | undefined) => at?.simTime ?? (isInfinite ? 0 : (at ? at.progress * durationVal : 0));
  const simTime = simAt(run);
  // Since the run last took a speed (#219), it paces from there: the delay counts from it, at that speed.
  const speed = run?.speed ?? Number(runSettings.speed ?? runDefaults.speed ?? 1);
  const since = run?.pacedFrom ? run.pacedFrom.at : run?.startedAt;
  const wallElapsed = since ? (now - since) / 1000 : 0;
  const simElapsed = simTime - (run?.pacedFrom ? simAt(run.pacedFrom) : 0);
  const expectedWall = speed > 0 ? simElapsed / speed : 0;
  const delaySeconds = speed > 0 && wallElapsed > 1.2 && simElapsed > 0 ? wallElapsed - expectedWall : 0;
  const isDelayed = delaySeconds > 0.2;

  const elapsed = run
    ? isInfinite
      ? `${simTime.toFixed(1)} s`
      : `${(run.progress * durationVal).toFixed(1)} / ${durationVal} s`
    : "";
  return { elapsed, delaySeconds, isDelayed };
}

/** The run settings as the dock shows them: during a run, the speed it runs at (run.speed), once it took one. */
export const dockSettings = (run: EngineState["run"], runSettings: RunSettings): RunSettings =>
  run?.status === "running" && run.speed !== undefined ? { ...runSettings, speed: run.speed } : runSettings;

/**
 * What to tell the backend when the dock's settings go from `before` to `after`: during a run, a
 * new speed (#219), which it takes at once; nothing otherwise (the next graph.run brings them).
 */
export function speedChange(running: boolean, before: RunSettings, after: RunSettings, defaults: RunSettings): ClientMsg | null {
  if (!running) return null;
  const speed = (s: RunSettings) => Number(s.speed ?? defaults.speed ?? 1);
  return speed(after) !== speed(before) ? { type: "run.speed", speed: speed(after) } : null;
}
