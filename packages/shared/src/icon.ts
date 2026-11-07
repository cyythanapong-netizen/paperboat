import type { IconDef, Prim } from "./protocol.ts";

/**
 * `view.icon` as plain SVG node data (contract §3). The token maps below are the only source of
 * attribute values: nothing from a descriptor reaches the SVG except as a key into them, or as a
 * number that passed the grid check. `iconNodes` returns null for any icon it cannot draw whole,
 * and the plate falls back to today's glyph plate.
 */

export const STROKE = { ink: "var(--text)", muted: "var(--line-strong)", signal: "var(--signal)" } as const;
export const WEIGHT = { thin: 1, normal: 1.5, wire: 2 } as const;
export const DASH = { solid: "none", axis: "2 2" } as const;
export const FILL = { none: "none", ink: "var(--text)", raised: "var(--surface-raised)", sunken: "var(--surface-sunken)" } as const;
export const CORNER = { "0": 0, sm: 3, md: 5 } as const; // --radius-sm, --radius-md in px
export const FONT = { sans: "var(--font-sans)", mono: "var(--font-mono)" } as const;
export const SIZE = [12, 12.5, 13, 15, 30] as const;

/** The plate: 1x1 is 64x56; 2x1 and 1x2 grow by whole cells (multiples of the 4px grid). */
export const CELL = { w: 64, h: 56 } as const;
const PAD = 8;
export const plateSize = (icon: Pick<IconDef, "w" | "h">) => ({ w: CELL.w * (icon.w ?? 1), h: CELL.h * (icon.h ?? 1) });

export type SvgNode = { tag: "line" | "polyline" | "polygon" | "rect" | "ellipse" | "path" | "text"; attrs: Record<string, string | number>; text?: string };

const own = (map: object, k: unknown): k is string => typeof k === "string" && Object.hasOwn(map, k);
const onGrid = (n: unknown): n is number => typeof n === "number" && Number.isFinite(n) && n >= 0 && n <= 100 && n % 5 === 0;
const warned = new Set<string>();

/** The whole icon is drawable: every primitive known, every token and size in its map, every coordinate on the grid. */
export function iconValid(icon: IconDef | undefined): icon is IconDef {
  if (!icon || typeof icon !== "object" || !Array.isArray(icon.g)) return false;
  if ((icon.w ?? 1) > 2 || (icon.h ?? 1) > 2 || ((icon.w ?? 1) === 2 && (icon.h ?? 1) === 2)) return false;
  if (![1, 2].includes(icon.w ?? 1) || ![1, 2].includes(icon.h ?? 1)) return false;
  return icon.g.every(primValid);
}

function primValid(p: Prim): boolean {
  if (!p || typeof p !== "object") return false;
  if (p.stroke !== undefined && !own(STROKE, p.stroke)) return false;
  if (p.weight !== undefined && !own(WEIGHT, p.weight)) return false;
  if (p.dash !== undefined && !own(DASH, p.dash)) return false;
  if (p.fill !== undefined && !own(FILL, p.fill)) return false;
  switch (p.t) {
    case "line":
    case "poly":
      return Array.isArray(p.p) && p.p.length >= 2 && p.p.every((pt) => Array.isArray(pt) && pt.length === 2 && onGrid(pt[0]) && onGrid(pt[1]));
    case "rect":
      return [p.x, p.y, p.w, p.h].every(onGrid) && (p.corner === undefined || own(CORNER, p.corner));
    case "ellipse":
      return [p.x, p.y, p.w, p.h].every(onGrid);
    case "arc":
      return [p.x, p.y, p.w, p.h].every(onGrid) && Number.isFinite(p.a0) && Number.isFinite(p.a1);
    case "text":
      return onGrid(p.x) && onGrid(p.y) && typeof p.s === "string" && (p.font === undefined || own(FONT, p.font)) && (p.size === undefined || (SIZE as readonly unknown[]).includes(p.size));
    default:
      return false;
  }
}

const r = (n: number) => +n.toFixed(2);

/**
 * The icon as SVG node data, sized to its plate, or null (after one `console.warn` per `type`)
 * when it cannot be drawn. `fill` puts the block's params into `{param}` in text.
 */
export function iconNodes(icon: IconDef | undefined, type = "", fill: (s: string) => string = (s) => s): SvgNode[] | null {
  if (!iconValid(icon)) {
    if (icon !== undefined && !warned.has(type)) {
      warned.add(type);
      console.warn(`block "${type}": view.icon is not drawable, drawing the plain plate`);
    }
    return null;
  }
  const { w, h } = plateSize(icon);
  const sx = (w - 2 * PAD) / 100;
  const sy = (h - 2 * PAD) / 100;
  const X = (x: number) => r(PAD + x * sx);
  const Y = (y: number) => r(PAD + y * sy);
  return icon.g.map((p): SvgNode => {
    const line = { stroke: STROKE[p.stroke ?? "ink"], "stroke-width": WEIGHT[p.weight ?? "normal"], "stroke-dasharray": DASH[p.dash ?? "solid"], "stroke-linecap": "round", "stroke-linejoin": "round" };
    const body = { ...line, fill: FILL[p.fill ?? "none"] };
    switch (p.t) {
      case "line":
        return { tag: "polyline", attrs: { ...line, fill: "none", points: p.p.map(([x, y]) => `${X(x)},${Y(y)}`).join(" ") } };
      case "poly":
        return { tag: "polygon", attrs: { ...body, points: p.p.map(([x, y]) => `${X(x)},${Y(y)}`).join(" ") } };
      case "rect":
        return { tag: "rect", attrs: { ...body, x: X(p.x), y: Y(p.y), width: r(p.w * sx), height: r(p.h * sy), rx: CORNER[p.corner ?? "0"] } };
      case "ellipse":
        return { tag: "ellipse", attrs: { ...body, cx: X(p.x + p.w / 2), cy: Y(p.y + p.h / 2), rx: r((p.w / 2) * sx), ry: r((p.h / 2) * sy) } };
      case "arc": {
        const [cx, cy, rx, ry] = [p.x + p.w / 2, p.y + p.h / 2, p.w / 2, p.h / 2];
        const at = (a: number) => `${X(cx + rx * Math.cos((a * Math.PI) / 180))},${Y(cy - ry * Math.sin((a * Math.PI) / 180))}`;
        const sweep = (((p.a1 - p.a0) % 360) + 360) % 360;
        return { tag: "path", attrs: { ...body, d: `M${at(p.a0)}A${r(rx * sx)},${r(ry * sy)} 0 ${sweep > 180 ? 1 : 0} 0 ${at(p.a1)}` } };
      }
      case "text":
        return {
          tag: "text",
          attrs: { x: X(p.x), y: Y(p.y), fill: STROKE[p.stroke ?? "ink"], stroke: "none", "text-anchor": "middle", "dominant-baseline": "central", "font-family": FONT[p.font ?? "sans"], "font-size": p.size ?? 12 },
          text: fill(p.s),
        };
    }
  });
}
