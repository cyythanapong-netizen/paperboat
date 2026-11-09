// Which way a port's signal goes (#204), in words. A turned or mirrored block puts its ports on any
// side, so "left is in, right is out" no longer holds: the dot draws a chevron (styles.css) and its
// hover title and accessible name say "input" or "output". Pure, so it can be tested; the DOM side
// is `usePortDirections`.

export type Direction = "in" | "out";

/** A handle's direction from its React Flow type: a target takes a wire in, a source sends one out. */
export const directionOf = (handleType: "source" | "target"): Direction => (handleType === "target" ? "in" : "out");

/** The port's type from a handle's or row's class list (`t-signal`, `port-numbers`), if it has one. */
export function typeFromClasses(classes: Iterable<string>): string | undefined {
  for (const c of classes) {
    const m = /^(?:t|port)-([a-z][a-z0-9_]*)$/.exec(c);
    // `port-row`, `port-strip`, `port-tip` are layout, not a type.
    if (m && !["row", "strip", "tip", "sign", "link", "undefined"].includes(m[1]!)) return m[1];
  }
  return undefined;
}

/** What a screen reader says for a port's dot: "input in, signal", "output y (m), numbers". */
export function directionName(dir: Direction, label: string, type?: string): string {
  return `${dir === "in" ? "input" : "output"} ${label}${type ? `, ${type}` : ""}`;
}

/** A port's hover title: its direction and type first, then what it said before (a unit, or why a wire won't fit). */
export function directionTitle(dir: Direction, type: string | undefined, base: string | undefined): string {
  const head = `${dir === "in" ? "Input" : "Output"}${type ? ` (${type})` : ""}`;
  return base ? `${head} · ${base}` : head;
}

/** The attributes of an element, all `keepAttr` and `writeAttr` need (so they can be tested without a DOM). */
export type Attrs = { getAttribute(name: string): string | null; setAttribute(name: string, value: string): void };

/**
 * What React put in `attr`, without the words added here: the attribute as it is, unless it is
 * still exactly what `writeAttr` last wrote (then the value that was written from).
 */
export function keepAttr(el: Attrs, attr: string): string {
  const now = el.getAttribute(attr) ?? "";
  return now === el.getAttribute(`data-dir-${attr}`) ? (el.getAttribute(`data-dir-${attr}-from`) ?? "") : now;
}

/** Sets `attr` to `value`, remembering what React had there; nothing is written when it already says that. */
export function writeAttr(el: Attrs, attr: string, value: string) {
  if (el.getAttribute(attr) === value) return;
  el.setAttribute(`data-dir-${attr}-from`, keepAttr(el, attr));
  el.setAttribute(`data-dir-${attr}`, value);
  el.setAttribute(attr, value);
}
