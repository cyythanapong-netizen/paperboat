// The omc expressions behind every Studio edit (contracts §8.2): pure text builders, so a test
// can say which expression an action sends. Each is one call on the allow-list (engine omc.cpp).
// The canvas works in px (y down, 20px grid); Modelica's diagram is units (y up), 2px a unit.

import type { Placement } from "../model/types.ts";

export const GRID = 20;
export const PX_PER_UNIT = 2;

const NAME = /^[A-Za-z_]\w*$/;
const CLASS = /^[A-Za-z_]\w*(\.[A-Za-z_]\w*)*$/;
const PATH = /^[A-Za-z_]\w*(\[\d+\])?(\.[A-Za-z_]\w*(\[\d+\])?)*$/;

function check(re: RegExp, what: string, text: string): string {
  if (!re.test(text)) throw new Error(`not ${what}: ${text}`);
  return text;
}

export const snap = (v: number, step = GRID) => Math.round(v / step) * step;
const num = (v: number) => String(+v.toFixed(3));

/** Canvas px to Modelica units. */
export const toUnits = (x: number, y: number): [number, number] => [x / PX_PER_UNIT + 0, -y / PX_PER_UNIT + 0]; // + 0: no -0
/** Modelica units to canvas px. */
export const toPx = (x: number, y: number): [number, number] => [x * PX_PER_UNIT + 0, -y * PX_PER_UNIT + 0];

export function placementAnnotation(at: Placement): string {
  const [x, y] = toUnits(at.x, at.y);
  const extent = at.mirror ? "{{10, -10}, {-10, 10}}" : "{{-10, -10}, {10, 10}}";
  return `Placement(visible=true, transformation=transformation(origin={${num(x)}, ${num(y)}}, extent=${extent}${at.rotation ? `, rotation=${at.rotation}` : ""}))`;
}

export function linePoints(points: [number, number][]): string {
  return `Line(points={${points.map(([x, y]) => `{${num(toUnits(x, y)[0])}, ${num(toUnits(x, y)[1])}}`).join(", ")}})`;
}

export const addComponent = (model: string, cls: string, name: string, at: Placement) =>
  `addComponent(${check(NAME, "a component name", name)}, ${check(CLASS, "a class name", cls)}, ${check(CLASS, "a model name", model)}, annotate=${placementAnnotation(at)})`;

/** A component with no diagram placement: a top-level parameter's declaration. */
export const addPlain = (model: string, cls: string, name: string) =>
  `addComponent(${check(NAME, "a component name", name)}, ${check(CLASS, "a class name", cls)}, ${check(CLASS, "a model name", model)})`;

export const updateComponent = (model: string, cls: string, name: string, at: Placement) =>
  `updateComponent(${check(NAME, "a component name", name)}, ${check(CLASS, "a class name", cls)}, ${check(CLASS, "a model name", model)}, annotate=${placementAnnotation(at)})`;

export const deleteComponent = (model: string, name: string) =>
  `deleteComponent(${check(NAME, "a component name", name)}, ${check(CLASS, "a model name", model)})`;

/** `a` and `b` are connector paths: "R1.p", or "u" for a connector component of the model. */
export const addConnection = (model: string, a: string, b: string, points?: [number, number][]) =>
  `addConnection(${check(PATH, "a connector", a)}, ${check(PATH, "a connector", b)}, ${check(CLASS, "a model name", model)}${points?.length ? `, annotate=${linePoints(points)}` : ""})`;

export const deleteConnection = (model: string, a: string, b: string) =>
  `deleteConnection(${check(PATH, "a connector", a)}, ${check(PATH, "a connector", b)}, ${check(CLASS, "a model name", model)})`;

/** "R1.R" sets a component's modifier. */
export const setModifier = (model: string, path: string, value: string) =>
  `setComponentModifierValue(${check(CLASS, "a model name", model)}, ${check(PATH, "a modifier", path)}, $Code(=${value.trim() || "0"}))`;

/** A top-level parameter's default ("J"), its binding. */
export const setParameter = (model: string, name: string, value: string) =>
  `setParameterValue(${check(CLASS, "a model name", model)}, ${check(NAME, "a parameter name", name)}, ${value.trim() || "0"})`;

export const clearModifier = (model: string, path: string) =>
  `setComponentModifierValue(${check(CLASS, "a model name", model)}, ${check(PATH, "a modifier", path)}, $Code())`;
