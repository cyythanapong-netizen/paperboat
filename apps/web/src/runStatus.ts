// The Run dock's words: why a run can't start, and how far one has got against the wall clock.

import type { EngineState } from "./engine";
import type { RunSettings } from "./protocol";

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
  const simTime = run?.simTime ?? (isInfinite ? 0 : (run ? run.progress * durationVal : 0));
  const speed = Number(runSettings.speed ?? runDefaults.speed ?? 1);
  const wallElapsed = run?.startedAt ? (now - run.startedAt) / 1000 : 0;
  const expectedWall = speed > 0 ? simTime / speed : 0;
  const delaySeconds = speed > 0 && wallElapsed > 1.2 && simTime > 0 ? wallElapsed - expectedWall : 0;
  const isDelayed = delaySeconds > 0.2;

  const elapsed = run
    ? isInfinite
      ? `${simTime.toFixed(1)} s`
      : `${(run.progress * durationVal).toFixed(1)} / ${durationVal} s`
    : "";
  return { elapsed, delaySeconds, isDelayed };
}
