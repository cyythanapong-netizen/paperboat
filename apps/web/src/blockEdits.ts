// Turning and mirroring a block on the Flow canvas: its placement changes, and its ports move to the
// sides they now face, so the wires follow.

import { flipPlacement, flipSideX, flipSideY, rotatePlacement, rotateSideCCW, rotateSideCW, type RotationDeg } from "@paperboat/shared";
import { placePorts, SIDES, type PortSides } from "./ports.ts";
import { inForm, type BlockNode } from "./flowModel.ts";

/** A quarter turn, clockwise or not. */
export function rotateBlock(n: BlockNode, cw: boolean): BlockNode {
  const currentRot = (n.data.rotation ?? 0) as RotationDeg;
  const currentMirror = Boolean(n.data.mirror);
  const { rotation: nextRot, mirror: nextMirror } = rotatePlacement({ rotation: currentRot, mirror: currentMirror }, cw);
  const def = inForm(n.data.def, n.data.params);
  const placed = placePorts(def, n.data.ports);
  const nextPorts: PortSides = {};
  for (const side of SIDES) {
    const nextSide = cw ? rotateSideCW(side) : rotateSideCCW(side);
    const list = placed[side].map((e) => e.port.id);
    if (list.length > 0) nextPorts[nextSide] = list;
  }
  return {
    ...n,
    data: {
      ...n.data,
      rotation: nextRot,
      mirror: nextMirror,
      ports: nextPorts,
    },
  };
}

/** Mirrored left to right (`x`) or top to bottom (`y`). */
export function flipBlock(n: BlockNode, axis: "x" | "y"): BlockNode {
  const currentRot = (n.data.rotation ?? 0) as RotationDeg;
  const currentMirror = Boolean(n.data.mirror);
  const { rotation: nextRot, mirror: nextMirror } = flipPlacement({ rotation: currentRot, mirror: currentMirror }, axis);
  const def = inForm(n.data.def, n.data.params);
  const placed = placePorts(def, n.data.ports);
  const nextPorts: PortSides = {};
  for (const side of SIDES) {
    const nextSide = axis === "x" ? flipSideX(side) : flipSideY(side);
    let list = placed[side].map((e) => e.port.id);
    if ((axis === "x" && (side === "top" || side === "bottom")) || (axis === "y" && (side === "left" || side === "right"))) {
      list = [...list].reverse();
    }
    if (list.length > 0) nextPorts[nextSide] = list;
  }
  return {
    ...n,
    data: {
      ...n.data,
      mirror: nextMirror,
      rotation: nextRot,
      ports: nextPorts,
    },
  };
}
