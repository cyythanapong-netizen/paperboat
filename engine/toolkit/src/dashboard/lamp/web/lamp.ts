import type { ParamValue } from "@paperboat/host";

/** The block's comparisons (its `op` param), as the instrument writes them. */
const OPS: Record<string, string> = { ">": ">", ">=": "≥", "<": "<", "<=": "≤", "==": "=", "!=": "≠" };

/** What a Lamp shows. The engine decides whether it is lit (threshold, op and latch are the block's); this only reads it. */
export interface LampView {
  /** Nothing received yet: the greyed placeholder. */
  idle: boolean;
  on: boolean;
  /** The state in words, never colour alone: `on`/`off`, or `tripped`/`armed` for a latching lamp; "–" before a run. */
  word: string;
  /** The lamp's own text (its `label` param), empty when none. */
  label: string;
  /** Its condition, as the caption shows it: "> 10", "≤ var". */
  condition: string;
  /** The colour it lights in: the signal colour, or red for a trip that means danger. */
  tone: "signal" | "danger";
  latch: boolean;
}

/** A param as text: a number trimmed, a bound one by its variable or formula. */
function show(v: ParamValue | undefined, fallback: string): string {
  if (v === undefined || v === "") return fallback;
  if (typeof v === "number") {
    const text = v !== 0 && Math.abs(v) < 1e-4 ? v.toExponential(2) : String(+v.toFixed(4));
    return text.replace("-", "−");
  }
  if (typeof v === "object") return "var" in v ? v.var : `ƒ ${v.expr}`;
  return String(v);
}

/** The Lamp's state from its latest sample (y: 1 or 0) and its params (the block's defaults where unset). */
export function lampView(latest: unknown, params: Record<string, ParamValue> = {}): LampView {
  const idle = latest === undefined;
  const on = typeof latest === "number" ? latest > 0.5 : latest === true;
  const latch = params.latch === true || (typeof params.latch === "number" && params.latch !== 0);
  const word = idle ? "–" : latch ? (on ? "tripped" : "armed") : on ? "on" : "off";
  const op = typeof params.op === "string" && params.op in OPS ? OPS[params.op] : OPS[">"];
  return {
    idle,
    on: !idle && on,
    word,
    label: typeof params.label === "string" ? params.label.trim() : "",
    condition: `${op} ${show(params.threshold, "0.5")}`,
    tone: params.color === "danger" ? "danger" : "signal",
    latch,
  };
}
