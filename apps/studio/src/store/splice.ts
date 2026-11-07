// Splicing a part into a selected wire (contracts §8.2): `/`, pick a class, and
//   Insert R2: R1.n -> p, n -> L1.p
// means: delete the wire R1.n-L1.p, add R2, connect R1.n to R2.p and R2.n to L1.p. The line
// shown before you confirm is built here, from the same plan the expressions come from.

import type { Connectors, IconPort } from "@paperboat/shared";
import type { Connection, Placement } from "../model/types.ts";
import * as x from "./exprs.ts";

export type SplicePlan = { line: string; exprs: string[]; at: Placement };

const family = (c: string) => (/^(Positive|Negative)Pin$/.test(c) ? "Pin" : c.replace(/_[ab]$/, ""));

/**
 * The two ports of the new part the wire runs through: for a physical wire, its first two ports
 * of the wire's connector family (R2.p, R2.n); for a causal one, its first input and first output.
 * null when the part has no way through (the words say why in `why`).
 */
export function throughPorts(ports: IconPort[], connectors: Connectors, wireClass: string): { into: string; out: string } | { why: string } {
  const kind = (p: IconPort) => connectors[p.class]?.kind;
  if (connectors[wireClass]?.kind === "acausal") {
    const same = ports.filter((p) => kind(p) === "acausal" && family(p.class) === family(wireClass));
    return same.length >= 2 ? { into: same[0].id, out: same[1].id } : { why: `it has fewer than two ${family(wireClass)} ports to run the wire through` };
  }
  const input = ports.find((p) => kind(p) === "input");
  const output = ports.find((p) => kind(p) === "output");
  return input && output ? { into: input.id, out: output.id } : { why: "it needs an input and an output to run a signal through" };
}

/**
 * `wire` is the connection to cut; `mid` where the new part goes (px, snapped here). The part
 * is entered at the wire's `a` end, or its `b` end when `reversed`: a causal wire runs output
 * to input, a physical one runs left to right, however omc happens to store it.
 */
export function splicePlan(model: string, wire: Connection, cls: string, name: string, through: { into: string; out: string }, mid: [number, number], reversed = false): SplicePlan {
  const [from, to] = reversed ? [wire.b, wire.a] : [wire.a, wire.b];
  const at: Placement = { x: x.snap(mid[0]), y: x.snap(mid[1]), rotation: 0 };
  return {
    line: `Insert ${name}: ${from} -> ${through.into}, ${through.out} -> ${to}`,
    exprs: [
      x.deleteConnection(model, wire.a, wire.b),
      x.addComponent(model, cls, name, at),
      x.addConnection(model, from, `${name}.${through.into}`),
      x.addConnection(model, `${name}.${through.out}`, to),
    ],
    at,
  };
}

/** The letters circuits name parts by; any other part takes its first letter. */
const LETTER: Record<string, string> = { Inductor: "L", Capacitor: "C", Ground: "G", SignalVoltage: "V", ConstantVoltage: "V", StepVoltage: "V", SignalCurrent: "I", ConstantCurrent: "I" };

/** R, L, C… the part's letter and the first free number: Resistor → R1, R2. */
export function freshName(cls: string, taken: Iterable<string>): string {
  const have = new Set(taken);
  const short = cls.slice(cls.lastIndexOf(".") + 1);
  const letter = LETTER[short] ?? (short[0] ?? "x").toUpperCase();
  for (let n = 1; ; n++) if (!have.has(`${letter}${n}`)) return `${letter}${n}`;
}
