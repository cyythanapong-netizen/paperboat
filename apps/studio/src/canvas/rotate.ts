// A placement's rotation and mirror applied to an icon: its graphics and its ports move together,
// so a vertical resistor draws vertical and its pins sit on the top and bottom. Pure data in, data
// out; the plate then draws it like any other icon.

import type { IconDef, IconPort, Prim } from "@paperboat/shared";
import type { Placement } from "../model/types.ts";

type Pt = [number, number];
type Side = IconPort["side"];

const turn: Record<Placement["rotation"], (p: Pt) => Pt> = {
  0: (p) => p,
  90: ([x, y]) => [y, 100 - x], // anticlockwise, y down
  180: ([x, y]) => [100 - x, 100 - y],
  270: ([x, y]) => [100 - y, x],
};

const flip = ([x, y]: Pt): Pt => [100 - x, y];
const at = (side: Side, a: number): Pt => (side === "left" ? [0, a] : side === "right" ? [100, a] : side === "top" ? [a, 0] : [a, 100]);
const sideOf = ([x, y]: Pt): { side: Side; at: number } =>
  x === 0 ? { side: "left", at: y } : x === 100 ? { side: "right", at: y } : y === 0 ? { side: "top", at: x } : { side: "bottom", at: x };

export function rotateIcon(icon: IconDef, place: Pick<Placement, "rotation" | "mirror">): IconDef {
  if (!place.rotation && !place.mirror) return icon;
  const move = (p: Pt) => turn[place.rotation](place.mirror ? flip(p) : p);
  const g = icon.g.map((prim): Prim => {
    switch (prim.t) {
      case "line":
      case "poly":
        return { ...prim, p: prim.p.map(move) };
      case "text": {
        const [x, y] = move([prim.x, prim.y]);
        return { ...prim, x, y };
      }
      default: {
        // rect, ellipse, arc: the box's two corners move, the box is what they span
        const [a, b] = [move([prim.x, prim.y]), move([prim.x + prim.w, prim.y + prim.h])];
        const box = { x: Math.min(a[0], b[0]), y: Math.min(a[1], b[1]), w: Math.abs(b[0] - a[0]), h: Math.abs(b[1] - a[1]) };
        if (prim.t !== "arc") return { ...prim, ...box };
        const a0 = (((prim.a0 + place.rotation) % 360) + 360) % 360; // the sweep stays as long as it was
        return { ...prim, ...box, a0, a1: a0 + (prim.a1 - prim.a0) };
      }
    }
  });
  const ports = icon.ports?.map((port) => ({ ...port, ...sideOf(move(at(port.side, port.at))) }));
  const swap = place.rotation === 90 || place.rotation === 270;
  const w = swap ? icon.h : icon.w;
  const h = swap ? icon.w : icon.h;
  return { ...(w && w !== 1 ? { w } : {}), ...(h && h !== 1 ? { h } : {}), g, ...(ports ? { ports } : {}) } as IconDef;
}
