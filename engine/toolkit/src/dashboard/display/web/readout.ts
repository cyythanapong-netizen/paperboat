/** How many values a Display lists before it shows only the first ones and the count (#24). */
export const SHOWN = 6;

const number = (v: number) => String(+v.toFixed(4));

/**
 * What a Display reads out: a number, text, or a list / vector. A list of up to SHOWN values is
 * shown whole (with its components' labels, `vector`, when it has them); a longer one as its first
 * values and an ellipsis. `count` is the list's length, for the caption; absent for a single value.
 */
export function readout(latest: unknown, vector?: string): { text: string; count?: number } {
  if (typeof latest === "number") return { text: number(latest) };
  if (Array.isArray(latest)) {
    const values = latest.filter((v): v is number => typeof v === "number");
    if (values.length <= SHOWN) return { text: `[${vector ?? values.map(number).join(", ")}]`, count: values.length };
    return { text: `[${values.slice(0, SHOWN).map(number).join(", ")}, …]`, count: values.length };
  }
  return { text: String(latest ?? "–") };
}
