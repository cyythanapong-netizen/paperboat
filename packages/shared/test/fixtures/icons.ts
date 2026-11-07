import type { Connectors, IconDef } from "../../src/protocol.ts";

/** Hand-written §3 icons, as the importer will emit them. */
export const Resistor: IconDef = {
  w: 2,
  g: [
    { t: "line", p: [[0, 50], [20, 50]] },
    { t: "rect", x: 20, y: 30, w: 60, h: 40, fill: "raised" },
    { t: "line", p: [[80, 50], [100, 50]] },
    { t: "text", x: 50, y: 85, s: "{R}", font: "mono", size: 12, stroke: "muted" },
  ],
  ports: [
    { id: "p", class: "PositivePin", side: "left", at: 50, variant: "filled" },
    { id: "n", class: "NegativePin", side: "right", at: 50, variant: "hollow" },
  ],
};
export const Ground: IconDef = {
  g: [
    { t: "line", p: [[50, 0], [50, 50]] },
    { t: "line", p: [[20, 50], [80, 50]], weight: "wire" },
    { t: "line", p: [[35, 70], [65, 70]], weight: "wire" },
    { t: "line", p: [[45, 90], [55, 90]], weight: "wire" },
  ],
  ports: [{ id: "p", class: "Pin", side: "top", at: 50 }],
};
export const Inertia: IconDef = {
  w: 2,
  g: [
    { t: "rect", x: 20, y: 25, w: 60, h: 50, fill: "sunken", corner: "sm" },
    { t: "arc", x: 30, y: 30, w: 40, h: 40, a0: 20, a1: 300, dash: "axis" },
    { t: "line", p: [[0, 50], [20, 50]], weight: "wire" },
    { t: "line", p: [[80, 50], [100, 50]], weight: "wire" },
    { t: "ellipse", x: 45, y: 45, w: 10, h: 10, fill: "ink" },
  ],
  ports: [
    { id: "flange_a", class: "Flange_a", side: "left", at: 50, variant: "filled" },
    { id: "flange_b", class: "Flange_b", side: "right", at: 50, variant: "hollow" },
  ],
};

export const connectors: Connectors = {
  Pin: { kind: "acausal", handle: "square", size: 8, weight: 1.2 },
  PositivePin: { kind: "acausal", handle: "square", size: 8, weight: 1.2, alt: "filled" },
  NegativePin: { kind: "acausal", handle: "square", size: 8, weight: 1.2 },
  Flange_a: { kind: "acausal", handle: "square", size: 10, weight: 1.8, alt: "filled" },
  Flange_b: { kind: "acausal", handle: "square", size: 10, weight: 1.8 },
  HeatPort_a: { kind: "acausal", handle: "diamond", size: 8, weight: 1.2, dash: "dashed" },
  RealInput: { kind: "input", handle: "triangle", size: 8, weight: 1.2 },
  RealOutput: { kind: "output", handle: "circle", size: 8, weight: 1.2, alt: "filled" },
};
