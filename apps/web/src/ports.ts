import type { BlockDef, ParamValue, PortDef, PortSides } from "./protocol";

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
  const vectors = (type: string) => type === "vector" || type === "numbers";
  const sameType = a === b || (vectors(a) && vectors(b));
  if ((a === "step") !== (b === "step")) return `joins ${article(a)} port to ${article(b)} port`;
  if (a !== "step" && !sameType && a !== "any" && b !== "any") return `joins ${article(a)} port to ${article(b)} port`;
  if (vectors(a) && vectors(b)) {
    const fromDimension = from.dimension ?? from.components?.length;
    const toDimension = to.dimension ?? to.components?.length;
    if (fromDimension !== undefined && toDimension !== undefined && fromDimension !== toDimension)
      return `carries a vector of ${fromDimension} values into a port expecting ${toDimension}`;
    const count = Math.max(from.components?.length ?? 0, to.components?.length ?? 0);
    for (let i = 0; i < count; i++) {
      const fromUnit = from.components?.[i]?.unit || from.unit;
      const toUnit = to.components?.[i]?.unit || to.unit;
      if (fromUnit && toUnit && fromUnit !== toUnit) return `carries vector component ${i} in ${fromUnit} into a port in ${toUnit}`;
    }
  } else if (from.unit && to.unit && from.unit !== to.unit) return `carries ${from.unit} into a port in ${to.unit}`;
  return null;
}

const article = (t: string) => `${/^[aeiou]/.test(t) ? "an" : "a"} ${t}`;

/** A port's name with its unit, for labels and titles: "force (N)". */
export const portLabel = (p: PortDef) => {
  const unit = p.unit ? ` (${p.unit})` : "";
  const dimension = p.dimension ?? p.components?.length;
  const size = (p.type === "vector" || p.type === "numbers") && dimension !== undefined ? ` [${dimension}]` : "";
  return `${p.id}${unit}${size}`;
};

/** A vector port's generic shape and components, for a hover title. */
export function portHint(p: PortDef): string | undefined {
  if (p.type !== "vector" && p.type !== "numbers") return p.unit ? `Unit: ${p.unit}` : undefined;
  const dimension = p.dimension ?? p.components?.length;
  const names = p.components?.map((c) => `${c.id}${c.unit ? ` (${c.unit})` : ""}`).join(", ");
  return `Ordered numeric vector${dimension === undefined ? " (dynamic length)" : ` (${dimension} values)`}${names ? `: ${names}` : p.unit ? `; unit: ${p.unit}` : ""}`;
}

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

/** The signs in a `signs` param, one per input: + is 1, - (or −) is −1, else skipped; at most 26; none reads as "++". Mirrors the engine's parse_signs. */
export function parseSigns(text: ParamValue | undefined): number[] {
  const signs = [...String(text ?? "")].flatMap((c) => (c === "+" ? [1] : c === "-" || c === "\u2212" ? [-1] : [])).slice(0, 26);
  return signs.length ? signs : [1, 1];
}

/** Scans a template string for {placeholder} tokens, skipping escaped {{ and }}, returning unique names in order. */
export function parsePlaceholders(text: ParamValue | undefined): string[] {
  const str = String(text ?? "");
  const out: string[] = [];
  const seen = new Set<string>();
  const regex = /\{\{|\}\}|\{([^}]+)\}/g;
  let m: RegExpExecArray | null;
  while ((m = regex.exec(str)) !== null) {
    if (m[1] !== undefined) {
      const name = m[1].trim();
      if (name && !name.includes("{") && !name.includes("}") && !seen.has(name)) {
        seen.add(name);
        out.push(name);
      }
    }
  }
  return out;
}

/** The sign shown beside input `i` of a block whose inputs come from signs: "+" or "−"; "" for any other block. */
export function signOf(def: BlockDef, params: Record<string, ParamValue>, i: number): string {
  if (!def.ports_from || def.ports_from !== "signs") return "";
  return parseSigns(params[def.ports_from])[i] < 0 ? "\u2212" : "+";
}

/** A block's definition as its params make it: a magic block in the form its `form` param picks, a block with `ports_from` (signs, template placeholders, or multi-input count). */
export function inForm(def: BlockDef, params: Record<string, ParamValue>): BlockDef {
  const form = def.forms?.find((f) => f.name === params.form);
  const base = form ? { ...def, inputs: form.inputs, outputs: form.outputs } : def;
  if (!def.ports_from) return base;
  if (def.ports_from === "template") {
    const tmpl = params.template ?? def.params.find((p) => p.id === "template")?.default ?? "Hello {name}";
    const names = parsePlaceholders(tmpl);
    return { ...base, inputs: names.map((name) => ({ id: name, type: "any" })) };
  }
  if (def.ports_from === "inputs" || typeof params[def.ports_from] === "number") {
    const countVal = typeof params[def.ports_from] === "number" ? Number(params[def.ports_from]) : 2;
    const count = Math.max(2, Math.min(26, Math.floor(countVal)));
    const proto = def.inputs[0] ?? { id: "a" };
    return { ...base, inputs: Array.from({ length: count }, (_, i) => ({ ...proto, id: String.fromCharCode(97 + i) })) };
  }
  const proto = def.inputs[0] ?? { id: "a" };
  return { ...base, inputs: parseSigns(params[def.ports_from]).map((_, i) => ({ ...proto, id: String.fromCharCode(97 + i) })) };
}
