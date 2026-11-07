// What each StudioApi action sends to omc, as plain data: the expressions, or the words why not.
// The hook in index.tsx only runs them; a test can read them here.

import type { Connection, StudioModel } from "../model/types.ts";
import * as x from "./exprs.ts";
import { freshName } from "./splice.ts";
import { endOf, hasWire, split, wireError } from "./wires.ts";

/** "R1.R" sets a part's modifier; "J" a top-level parameter's default, made first when it isn't there. */
export function setParamExprs(model: StudioModel, path: string, value: string): string[] {
  if (path.includes(".")) return [x.setModifier(model.name, path, value)];
  return model.params.some((p) => p.id === path)
    ? [x.setParameter(model.name, path, value)]
    : [x.addPlain(model.name, "Real", path), x.setParameter(model.name, path, value)];
}

/** One wire a-b: refused in words (the §4 rules, a wire that is already there), or the one expression. */
export function connectExprs(model: StudioModel, a: string, b: string): { error: string } | { exprs: string[] } {
  const why = wireError(model, a, b);
  if (why) return { error: why };
  if (hasWire(model, a, b)) return { error: "those two are already wired" };
  return { exprs: [x.addConnection(model.name, a, b)] };
}

/** `+ Input` / `+ Probe`: a RealInput or RealOutput on the model, wired to the port `target`. */
export function entryExprs(model: StudioModel, kind: "input" | "output", target: string): string[] {
  if (!target.includes(".") || !endOf(model, target)) throw new Error("a probe or input needs a port to attach to; variables need an annotation in the .mo for now");
  const taken = model.components.map((c) => c.name);
  const name = freshName(kind === "input" ? "u" : "y", taken).toLowerCase().replace(/1$/, "");
  const part = model.components.find((c) => c.name === split(target).part);
  const at = { x: x.snap((part?.placement.x ?? 0) + (kind === "input" ? -200 : 200)), y: part?.placement.y ?? 0, rotation: 0 as const };
  const cls = kind === "input" ? "Modelica.Blocks.Interfaces.RealInput" : "Modelica.Blocks.Interfaces.RealOutput";
  return [x.addComponent(model.name, cls, name, at), kind === "input" ? x.addConnection(model.name, name, target) : x.addConnection(model.name, target, name)];
}

export function removeEntryExprs(model: StudioModel, entry: string): string[] {
  const wires = model.connections.filter((c) => c.a === entry || c.b === entry);
  return [...wires.map((w) => x.deleteConnection(model.name, w.a, w.b)), x.deleteComponent(model.name, entry)];
}

/** Delete parts (and every wire on them) and the wires picked, wires first. */
export function removeExprs(model: StudioModel, names: string[], wires: Connection[]): string[] {
  const gone = new Set(names);
  const cut = [...wires, ...model.connections.filter((c) => gone.has(split(c.a).part) || gone.has(split(c.b).part))];
  const seen = new Set<string>();
  const out: string[] = [];
  for (const w of cut) {
    const k = [w.a, w.b].sort().join("|");
    if (!seen.has(k)) out.push(x.deleteConnection(model.name, w.a, w.b));
    seen.add(k);
  }
  return [...out, ...names.map((n) => x.deleteComponent(model.name, n))];
}

/** The nearest free grid spot at or after `from` (a drop, the view's centre): parts never land on one another. */
export function freeSpot(model: StudioModel, from: { x: number; y: number } = { x: 200, y: 160 }): { x: number; y: number; rotation: 0 } {
  const taken = (p: { x: number; y: number }) => model.components.some((c) => Math.abs(c.placement.x - p.x) < 100 && Math.abs(c.placement.y - p.y) < 80);
  const start = { x: x.snap(from.x), y: x.snap(from.y) };
  for (let i = 0; i < 200; i++) {
    const p = { x: start.x + (i % 6) * 140, y: start.y + Math.floor(i / 6) * 120 };
    if (!taken(p)) return { ...p, rotation: 0 };
  }
  return { ...start, rotation: 0 };
}
