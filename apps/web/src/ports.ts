import type { BlockDef, PortDef, PortSides } from "./protocol";

/**
 * Why a wire may not join output `from` to input `to` (#4), or null if it may. The same
 * rule as the engine's `toolkit::wire_mismatch`, which checks again when a graph runs; the UI
 * uses it while dragging so a wire only snaps where it fits.
 *
 * A wire joins ports of one type (signal, image, points…); `any` joins anything but a step;
 * step ports only join step ports. With a unit on both ends they must agree.
 */
export function wireMismatch(from: PortDef, to: PortDef): string | null {
  const a = from.type ?? "signal";
  const b = to.type ?? "signal";
  if ((a === "step") !== (b === "step")) return `joins ${article(a)} port to ${article(b)} port`;
  if (a !== "step" && a !== b && a !== "any" && b !== "any") return `joins ${article(a)} port to ${article(b)} port`;
  if (from.unit && to.unit && from.unit !== to.unit) return `carries ${from.unit} into a port in ${to.unit}`;
  return null;
}

const article = (t: string) => `${/^[aeiou]/.test(t) ? "an" : "a"} ${t}`;

/** A port's name with its unit, for labels and titles: "force (N)". */
export const portLabel = (p: PortDef) => (p.unit ? `${p.id} (${p.unit})` : p.id);

/** A side of a block a port can sit on. */
export type Side = "left" | "right" | "top" | "bottom";
export const SIDES: readonly Side[] = ["left", "right", "top", "bottom"];
export type { PortSides };
/** A port as placed on a block: an input (`in`) or an output (`out`). */
export type Placed = { port: PortDef; dir: "in" | "out" };

/**
 * A block's ports by side, in order. Ports listed in `sides` go where it says; the rest
 * keep the default place after them: inputs on the left, outputs on the right. An id that
 * names no port is skipped, so a layout survives its block type losing a port.
 */
export function placePorts(def: BlockDef, sides: PortSides | undefined): Record<Side, Placed[]> {
  const all: Placed[] = [...def.inputs.map((port) => ({ port, dir: "in" as const })), ...def.outputs.map((port) => ({ port, dir: "out" as const }))];
  const out: Record<Side, Placed[]> = { left: [], right: [], top: [], bottom: [] };
  const placed = new Set<Placed>();
  for (const side of SIDES)
    for (const id of sides?.[side] ?? [])
      for (const e of all)
        if (e.port.id === id && !placed.has(e)) {
          out[side].push(e);
          placed.add(e);
        }
  for (const e of all) if (!placed.has(e)) out[e.dir === "in" ? "left" : "right"].push(e);
  return out;
}

/**
 * The layout with port `id` moved to `side`, `index`th among the other ports there.
 * undefined when that is the default layout, so a block whose ports are all home saves no `ports`.
 */
export function movePort(def: BlockDef, sides: PortSides | undefined, id: string, side: Side, index: number): PortSides | undefined {
  const lists = without(placePorts(def, sides), id);
  lists[side].splice(Math.max(0, Math.min(index, lists[side].length)), 0, id);
  return tidy(def, lists);
}

/** The layout with port `id` back in its default place (after the ports listed there). */
export function resetPort(def: BlockDef, sides: PortSides | undefined, id: string): PortSides | undefined {
  const lists = without(placePorts(def, sides), id);
  const rest = placePorts(def, tidy(def, lists));
  return tidy(def, without(rest, ""));
}

const without = (placed: Record<Side, Placed[]>, id: string) =>
  Object.fromEntries(SIDES.map((s) => [s, placed[s].map((e) => e.port.id).filter((x) => x !== id)])) as Record<Side, string[]>;

function tidy(def: BlockDef, lists: Record<Side, string[]>): PortSides | undefined {
  const home = without(placePorts(def, undefined), "");
  if (SIDES.every((s) => lists[s].join("\n") === home[s].join("\n"))) return undefined;
  return Object.fromEntries(SIDES.filter((s) => lists[s].length > 0).map((s) => [s, lists[s]]));
}
