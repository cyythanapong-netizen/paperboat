// Where a part and its ports sit on the canvas, in px. A placement is the centre of the plate;
// a port sits on the plate's edge, `at` percent along it.

import { plateSize } from "@paperboat/shared";
import type { ComponentInst } from "../model/types.ts";
import { rotateIcon } from "./rotate.ts";

const PLAIN = { g: [] };

/** The part's icon as placed (turned and mirrored). */
export const placedIcon = (part: ComponentInst) => rotateIcon(part.icon ?? PLAIN, part.placement);

export const plateOf = (part: ComponentInst) => plateSize(placedIcon(part));

/** The plate's top-left, where the canvas puts the node. */
export function topLeft(part: ComponentInst): [number, number] {
  const { w, h } = plateOf(part);
  return [part.placement.x - w / 2, part.placement.y - h / 2];
}

/** The plate centre for a node dragged to `position`. */
export function centreFor(part: ComponentInst, position: { x: number; y: number }): [number, number] {
  const { w, h } = plateOf(part);
  return [position.x + w / 2, position.y + h / 2];
}

/** A wire's drawn points: the ports at each end and the bends between, each bend turned square with its neighbour. */
export function squareUp(from: [number, number], bends: [number, number][], to: [number, number]): [number, number][] {
  const mid = bends.map((b): [number, number] => [...b]);
  const fix = (b: [number, number], end: [number, number]) => {
    if (Math.abs(b[1] - end[1]) <= Math.abs(b[0] - end[0])) b[1] = end[1];
    else b[0] = end[0];
  };
  if (mid.length) {
    fix(mid[0], from);
    fix(mid[mid.length - 1], to);
  }
  return [from, ...mid, to];
}
