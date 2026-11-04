import type { BlockDef, ParamValue, PortDef } from "./protocol";

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

/** The signs in a `signs` param, one per input: + is 1, - (or −) is −1, else skipped; at most 26; none reads as "++". Mirrors the engine's parse_signs. */
export function parseSigns(text: ParamValue | undefined): number[] {
  const signs = [...String(text ?? "")].flatMap((c) => (c === "+" ? [1] : c === "-" || c === "\u2212" ? [-1] : [])).slice(0, 26);
  return signs.length ? signs : [1, 1];
}

/** The sign shown beside input `i` of a block whose inputs come from signs: "+" or "−"; "" for any other block. */
export function signOf(def: BlockDef, params: Record<string, ParamValue>, i: number): string {
  if (!def.ports_from) return "";
  return parseSigns(params[def.ports_from])[i] < 0 ? "\u2212" : "+";
}
