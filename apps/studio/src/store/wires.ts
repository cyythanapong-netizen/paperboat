// Which wires are valid (contracts §4), in the words the canvas shows. The rule itself is
// `connectError` in @paperboat/shared; this only finds the two ends in the model.

import { connectError, iconEnd, type Connectors, type IconPort, type WireEnd } from "@paperboat/shared";
import type { ComponentInst, StudioModel } from "../model/types.ts";

/** "R1.p" is part R1, port p; "u" (no dot) is the connector component u, whose one port is its own name. */
export function split(path: string): { part: string; port: string } {
  const dot = path.indexOf(".");
  return dot < 0 ? { part: path, port: path } : { part: path.slice(0, dot), port: path.slice(dot + 1) };
}
export const pathOf = (part: ComponentInst, port: string) => (isConnector(part) ? part.name : `${part.name}.${port}`);

/** A connector component of the model itself (a top-level `RealInput u`): its one port is the part. */
export const isConnector = (part: ComponentInst) => part.ports.length === 1 && part.ports[0].id === part.name;

export function portOf(model: StudioModel, path: string): { part: ComponentInst; port: IconPort } | null {
  const { part, port } = split(path);
  const p = model.components.find((c) => c.name === part);
  const q = p?.ports.find((x) => x.id === port.replace(/\[\d+\]$/, ""));
  return p && q ? { part: p, port: q } : null;
}

/** One end of a wire. The model's own connectors are "inside": they connect through, so an input u feeds an inner input. */
export function endOf(model: StudioModel, path: string): WireEnd | null {
  const found = portOf(model, path);
  if (!found) return null;
  const end = iconEnd(found.port, model.connectors);
  return end && isConnector(found.part) ? { ...end, inside: true } : end;
}

/** The words why `a` (where the wire starts) can't join `b`, or null. */
export function wireError(model: StudioModel, a: string, b: string): string | null {
  if (split(a).part === split(b).part && !isConnector(model.components.find((c) => c.name === split(a).part)!)) {
    if (a === b) return "a port does not join itself";
  }
  const from = endOf(model, a);
  const to = endOf(model, b);
  if (!from || !to) return `no port ${!from ? a : b} to join`;
  return connectError(from, to);
}

/** Does the wire a-b already exist (either way round)? */
export const hasWire = (model: StudioModel, a: string, b: string) => model.connections.some((c) => (c.a === a && c.b === b) || (c.a === b && c.b === a));

export const kindOf = (connectors: Connectors, port: IconPort) => connectors[port.class]?.kind;
