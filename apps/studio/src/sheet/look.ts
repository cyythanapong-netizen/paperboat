import {
  CORNER,
  DASH,
  FILL,
  FONT,
  iconValid,
  SIZE,
  STROKE,
  WEIGHT,
  type BlockDef,
  type Corner,
  type Dash,
  type Fill,
  type IconDef,
  type IconPort,
  type Prim,
  type Pt,
  type Stroke,
  type Weight,
} from "@paperboat/shared";
import type { EntryPoint, StudioModel } from "../model/types.ts";

export type PlateSizeKey = "1x1" | "2x1" | "1x2";
export const PLATE_SIZES: readonly PlateSizeKey[] = ["1x1", "2x1", "1x2"];

export const PRIM_KINDS: readonly Prim["t"][] = ["line", "poly", "rect", "ellipse", "arc", "text"];

export const STROKE_OPTIONS: { value: Stroke; label: string }[] = [
  { value: "ink", label: "ink" },
  { value: "muted", label: "muted" },
  { value: "signal", label: "signal" },
];

export const WEIGHT_OPTIONS: { value: Weight; label: string }[] = [
  { value: "thin", label: "thin" },
  { value: "normal", label: "normal" },
  { value: "wire", label: "wire" },
];

export const FILL_OPTIONS: { value: Fill; label: string }[] = [
  { value: "none", label: "none" },
  { value: "ink", label: "ink" },
  { value: "raised", label: "raised" },
  { value: "sunken", label: "sunken" },
];

export const DASH_OPTIONS: { value: Dash; label: string }[] = [
  { value: "solid", label: "solid" },
  { value: "axis", label: "axis" },
];

export const CORNER_OPTIONS: { value: Corner; label: string }[] = [
  { value: "0", label: "0" },
  { value: "sm", label: "sm" },
  { value: "md", label: "md" },
];

export const FONT_OPTIONS: { value: "sans" | "mono"; label: string }[] = [
  { value: "sans", label: "sans" },
  { value: "mono", label: "mono" },
];

export const SIZE_OPTIONS: { value: "12" | "12.5" | "13" | "15" | "30"; label: string }[] = [
  { value: "12", label: "12" },
  { value: "12.5", label: "12.5" },
  { value: "13", label: "13" },
  { value: "15", label: "15" },
  { value: "30", label: "30" },
];

export const SIDE_OPTIONS: { value: IconPort["side"]; label: string }[] = [
  { value: "left", label: "left" },
  { value: "right", label: "right" },
  { value: "top", label: "top" },
  { value: "bottom", label: "bottom" },
];

/** Snaps any coordinate to the 0..100 box in multiples of 5 (§3). */
export function snapTo5(n: number): number {
  if (!Number.isFinite(n)) return 50;
  const clamped = Math.max(0, Math.min(100, Math.round(n / 5) * 5));
  return clamped === 0 ? 0 : clamped;
}

export function plateSizeKey(icon: IconDef | undefined): PlateSizeKey {
  if (icon?.w === 2 && (icon?.h ?? 1) === 1) return "2x1";
  if ((icon?.w ?? 1) === 1 && icon?.h === 2) return "1x2";
  return "1x1";
}

export function applyPlateSize(icon: IconDef, size: PlateSizeKey): IconDef {
  const [w, h]: [1 | 2, 1 | 2] = size === "2x1" ? [2, 1] : size === "1x2" ? [1, 2] : [1, 1];
  return { ...icon, w, h };
}

export type PrimStyle = {
  stroke?: Stroke;
  weight?: Weight;
  dash?: Dash;
  fill?: Fill;
  corner?: Corner;
  font?: "sans" | "mono";
  size?: 12 | 12.5 | 13 | 15 | 30;
};

function cleanStyle(style?: PrimStyle): Pick<Prim, "stroke" | "weight" | "dash" | "fill"> {
  const out: Pick<Prim, "stroke" | "weight" | "dash" | "fill"> = {};
  if (style?.stroke && Object.hasOwn(STROKE, style.stroke)) out.stroke = style.stroke;
  if (style?.weight && Object.hasOwn(WEIGHT, style.weight)) out.weight = style.weight;
  if (style?.dash && Object.hasOwn(DASH, style.dash)) out.dash = style.dash;
  if (style?.fill && Object.hasOwn(FILL, style.fill)) out.fill = style.fill;
  return out;
}

export function createDefaultPrim(kind: Prim["t"], style?: PrimStyle): Prim {
  const base = cleanStyle(style);
  switch (kind) {
    case "line":
      return { t: "line", p: [[20, 50], [80, 50]], ...base };
    case "poly":
      return { t: "poly", p: [[30, 25], [75, 50], [30, 75]], ...base };
    case "rect":
      return {
        t: "rect",
        x: 20,
        y: 25,
        w: 60,
        h: 50,
        corner: style?.corner && Object.hasOwn(CORNER, style.corner) ? style.corner : "0",
        ...base,
      };
    case "ellipse":
      return { t: "ellipse", x: 30, y: 30, w: 40, h: 40, ...base };
    case "arc":
      return { t: "arc", x: 25, y: 25, w: 50, h: 50, a0: 0, a1: 270, ...base };
    case "text":
      return {
        t: "text",
        x: 50,
        y: 50,
        s: "M",
        font: style?.font && Object.hasOwn(FONT, style.font) ? style.font : "mono",
        size: style?.size && (SIZE as readonly number[]).includes(style.size) ? style.size : 12,
        ...base,
      };
  }
}

export function addPrimToIcon(icon: IconDef, kind: Prim["t"], style?: PrimStyle): IconDef {
  return {
    ...icon,
    g: [...icon.g, createDefaultPrim(kind, style)],
  };
}

export function movePrimBy(prim: Prim, dx: number, dy: number): Prim {
  const sdx = Math.round(dx / 5) * 5;
  const sdy = Math.round(dy / 5) * 5;
  switch (prim.t) {
    case "line":
    case "poly": {
      const xs = prim.p.map((pt) => pt[0]);
      const ys = prim.p.map((pt) => pt[1]);
      const minX = Math.min(...xs);
      const maxX = Math.max(...xs);
      const minY = Math.min(...ys);
      const maxY = Math.max(...ys);
      const clampDx = Math.max(-minX, Math.min(100 - maxX, sdx));
      const clampDy = Math.max(-minY, Math.min(100 - maxY, sdy));
      const p: Pt[] = prim.p.map(([x, y]) => [snapTo5(x + clampDx), snapTo5(y + clampDy)]);
      return { ...prim, p };
    }
    case "rect":
    case "ellipse":
    case "arc": {
      const w = Math.max(5, snapTo5(prim.w));
      const h = Math.max(5, snapTo5(prim.h));
      const x = Math.max(0, Math.min(100 - w, snapTo5(prim.x + sdx)));
      const y = Math.max(0, Math.min(100 - h, snapTo5(prim.y + sdy)));
      return { ...prim, x, y, w, h };
    }
    case "text":
      return {
        ...prim,
        x: snapTo5(prim.x + sdx),
        y: snapTo5(prim.y + sdy),
      };
  }
}

export function updatePrimTokens(
  prim: Prim,
  patch: Partial<
    PrimStyle & {
      s: string;
      x: number;
      y: number;
      w: number;
      h: number;
      a0: number;
      a1: number;
    }
  >,
): Prim {
  const next: Record<string, unknown> = { ...prim };
  if (patch.stroke !== undefined && Object.hasOwn(STROKE, patch.stroke)) next.stroke = patch.stroke;
  if (patch.weight !== undefined && Object.hasOwn(WEIGHT, patch.weight)) next.weight = patch.weight;
  if (patch.dash !== undefined && Object.hasOwn(DASH, patch.dash)) next.dash = patch.dash;
  if (patch.fill !== undefined && Object.hasOwn(FILL, patch.fill)) next.fill = patch.fill;
  if (prim.t === "rect" && patch.corner !== undefined && Object.hasOwn(CORNER, patch.corner)) {
    next.corner = patch.corner;
  }
  if (prim.t === "text") {
    if (patch.font !== undefined && Object.hasOwn(FONT, patch.font)) next.font = patch.font;
    if (patch.size !== undefined && (SIZE as readonly number[]).includes(patch.size)) next.size = patch.size;
    if (patch.s !== undefined) next.s = String(patch.s);
  }
  if (prim.t === "arc") {
    if (patch.a0 !== undefined && Number.isFinite(patch.a0)) next.a0 = Math.round(patch.a0);
    if (patch.a1 !== undefined && Number.isFinite(patch.a1)) next.a1 = Math.round(patch.a1);
  }
  if (patch.x !== undefined && "x" in prim) next.x = snapTo5(patch.x);
  if (patch.y !== undefined && "y" in prim) next.y = snapTo5(patch.y);
  if (patch.w !== undefined && "w" in prim) next.w = Math.max(5, snapTo5(patch.w));
  if (patch.h !== undefined && "h" in prim) next.h = Math.max(5, snapTo5(patch.h));
  return sanitizePrim(next as Prim);
}

export function removePrimAt(icon: IconDef, index: number): IconDef {
  return {
    ...icon,
    g: icon.g.filter((_, i) => i !== index),
  };
}

export function sanitizePrim(p: Prim): Prim {
  const base = cleanStyle(p);
  switch (p.t) {
    case "line":
    case "poly": {
      const pts: Pt[] = (Array.isArray(p.p) && p.p.length >= 2 ? p.p : [[20, 50], [80, 50]]).map(([x, y]) => [
        snapTo5(x),
        snapTo5(y),
      ]);
      return { t: p.t, p: pts, ...base };
    }
    case "rect":
      return {
        t: "rect",
        x: snapTo5(p.x),
        y: snapTo5(p.y),
        w: Math.max(5, snapTo5(p.w)),
        h: Math.max(5, snapTo5(p.h)),
        ...(p.corner && Object.hasOwn(CORNER, p.corner) ? { corner: p.corner } : {}),
        ...base,
      };
    case "ellipse":
      return {
        t: "ellipse",
        x: snapTo5(p.x),
        y: snapTo5(p.y),
        w: Math.max(5, snapTo5(p.w)),
        h: Math.max(5, snapTo5(p.h)),
        ...base,
      };
    case "arc":
      return {
        t: "arc",
        x: snapTo5(p.x),
        y: snapTo5(p.y),
        w: Math.max(5, snapTo5(p.w)),
        h: Math.max(5, snapTo5(p.h)),
        a0: Number.isFinite(p.a0) ? Math.round(p.a0) : 0,
        a1: Number.isFinite(p.a1) ? Math.round(p.a1) : 270,
        ...base,
      };
    case "text":
      return {
        t: "text",
        x: snapTo5(p.x),
        y: snapTo5(p.y),
        s: typeof p.s === "string" ? p.s : "",
        ...(p.font && Object.hasOwn(FONT, p.font) ? { font: p.font } : {}),
        ...(p.size !== undefined && (SIZE as readonly number[]).includes(p.size) ? { size: p.size } : {}),
        ...base,
      };
  }
}

export function syncPortsWithEntries(icon: IconDef | undefined, entries: EntryPoint[]): IconDef {
  const existingPorts = icon?.ports ?? [];
  const inputs = entries.filter((e) => e.kind === "input");
  const outputs = entries.filter((e) => e.kind === "output");

  const defaultAt = (idx: number, count: number) => snapTo5(((idx + 1) / (count + 1)) * 100);

  const syncedPorts: IconPort[] = entries.map((entry) => {
    const prev = existingPorts.find((p) => p.id === entry.name);
    const isInput = entry.kind === "input";
    const list = isInput ? inputs : outputs;
    const idx = Math.max(0, list.findIndex((e) => e.name === entry.name));
    const side: IconPort["side"] = prev?.side ?? (isInput ? "left" : "right");
    const at = prev ? snapTo5(prev.at) : defaultAt(idx, list.length);
    return {
      id: entry.name,
      class: isInput ? "RealInput" : "RealOutput",
      side,
      at,
      ...(isInput ? {} : { variant: "filled" as const }),
    };
  });

  const base: IconDef = icon ?? { w: 1, h: 1, g: [] };
  return sanitizeIconDef({ ...base, ports: syncedPorts });
}

export function setPortPlacement(
  icon: IconDef,
  entries: EntryPoint[],
  portId: string,
  side: IconPort["side"],
  at: number,
): IconDef {
  const synced = syncPortsWithEntries(icon, entries);
  const snappedAt = snapTo5(at);
  const ports = (synced.ports ?? []).map((p) => (p.id === portId ? { ...p, side, at: snappedAt } : p));
  return sanitizeIconDef({ ...synced, ports });
}

export function sanitizeIconDef(icon: IconDef, entries?: EntryPoint[]): IconDef {
  const size = plateSizeKey(icon);
  const [w, h]: [1 | 2, 1 | 2] = size === "2x1" ? [2, 1] : size === "1x2" ? [1, 2] : [1, 1];
  const g = Array.isArray(icon.g) ? icon.g.map(sanitizePrim) : [];
  let ports = icon.ports?.map((p) => ({
    ...p,
    at: snapTo5(p.at),
  }));
  if (entries) {
    ports = syncPortsWithEntries({ w, h, g, ports }, entries).ports;
  }
  const candidate: IconDef = { w, h, g, ...(ports ? { ports } : {}) };
  return iconValid(candidate) ? candidate : { w: 1, h: 1, g: [] };
}

export function buildLinkedBlockDef(model: StudioModel, overrideIcon?: IconDef): BlockDef {
  const icon = syncPortsWithEntries(overrideIcon ?? model.icon, model.entries);
  const shortName = model.name.split(".").pop() ?? model.name;
  return {
    type: shortName.toLowerCase(),
    label: shortName,
    kind: "function",
    linked: true,
    feedthrough: false,
    inputs: model.entries
      .filter((e) => e.kind === "input")
      .map((e) => ({ id: e.name, type: "signal", ...(e.unit ? { unit: e.unit } : {}) })),
    outputs: model.entries
      .filter((e) => e.kind === "output")
      .map((e) => ({ id: e.name, type: "signal", ...(e.unit ? { unit: e.unit } : {}) })),
    params: [],
    view: {
      compact: true,
      icon,
    },
  };
}
