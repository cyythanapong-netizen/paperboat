// Undo and redo through omc: the history holds snapshots of the canvas (shared undo.ts), and
// going back to one means making omc's model equal to it. This turns "model now" and "model then"
// into the omc expressions that get from one to the other, wires out first and in last.

import type { Placement } from "../model/types.ts";
import * as x from "./exprs.ts";

export type PartSnap = { class: string; params: Record<string, string>; placement: Placement };
export type WireSnap = { a: string; b: string; points?: [number, number][] };
export type Graph = { parts: Record<string, PartSnap>; wires: WireSnap[] };

const key = (w: WireSnap) => [w.a, w.b].sort().join("|");
const same = (p: Placement, q: Placement) => p.x === q.x && p.y === q.y && p.rotation === q.rotation && !!p.mirror === !!q.mirror;
const bends = (w: WireSnap) => JSON.stringify(w.points ?? []);

export function restoreExprs(model: string, now: Graph, then: Graph): string[] {
  const out: string[] = [];
  const thenWires = new Map(then.wires.map((w) => [key(w), w]));
  const nowWires = new Map(now.wires.map((w) => [key(w), w]));
  // a wire that must change bends is deleted and added again
  const dropped = now.wires.filter((w) => !thenWires.has(key(w)) || bends(thenWires.get(key(w))!) !== bends(w));
  for (const w of dropped) out.push(x.deleteConnection(model, w.a, w.b));
  for (const name of Object.keys(now.parts)) if (!then.parts[name]) out.push(x.deleteComponent(model, name));
  for (const [name, p] of Object.entries(then.parts)) {
    const was = now.parts[name];
    if (!was) {
      out.push(x.addComponent(model, p.class, name, p.placement));
      for (const [k, v] of Object.entries(p.params)) out.push(x.setModifier(model, `${name}.${k}`, v));
      continue;
    }
    if (!same(was.placement, p.placement)) out.push(x.updateComponent(model, p.class, name, p.placement));
    for (const [k, v] of Object.entries(p.params)) if (was.params[k] !== v) out.push(x.setModifier(model, `${name}.${k}`, v));
    for (const k of Object.keys(was.params)) if (!(k in p.params)) out.push(x.clearModifier(model, `${name}.${k}`));
  }
  for (const w of then.wires) {
    const before = nowWires.get(key(w));
    if (!before || bends(before) !== bends(w)) out.push(x.addConnection(model, w.a, w.b, w.points));
  }
  return out;
}
