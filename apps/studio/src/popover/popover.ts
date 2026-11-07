import type { Connectors, IconPort } from "@paperboat/shared";
import type { ComponentInst, StudioApi, StudioModel } from "../model/types.ts";

function parseNumber(text: string): number | undefined {
  if (text.trim() === "") return undefined;
  const v = Number(text);
  return Number.isFinite(v) ? v : undefined;
}

const PARAM_UNITS: Record<string, string> = {
  R: "Ohm",
  L: "H",
  C: "F",
  G: "S",
  J: "kg.m2",
  m: "kg",
  c: "N/m",
  d: "N.s/m",
  phi0: "rad",
  w0: "rad/s",
  s0: "m",
  v0: "m/s",
  T: "K",
  T_ref: "K",
  alpha: "1/K",
  V: "V",
  k: "N.m/A",
  f: "Hz",
};

export function paramUnit(paramId: string, model: StudioModel, boundTo?: string | null): string | undefined {
  if (boundTo) {
    const top = model.params.find((p) => p.id === boundTo);
    if (top && top.type === "number" && top.unit) return top.unit;
  }
  const same = model.params.find((p) => p.id === paramId);
  if (same && same.type === "number" && same.unit) return same.unit;
  return PARAM_UNITS[paramId];
}

export type ParsedParam = {
  bound: string | null;
  numeric: number | null;
  modifierText: string;
};

/**
 * Inspects a component modifier string (`"10"`, `"R_load"`, or `"R = R_load"`).
 */
export function parseParamBinding(paramId: string, rawValue: string): ParsedParam {
  const trimmed = rawValue.trim();
  const prefix = new RegExp(`^${paramId}\\s*=\\s*`);
  const rhs = trimmed.replace(prefix, "").trim();
  const num = parseNumber(rhs);
  if (num !== undefined) {
    return { bound: null, numeric: num, modifierText: String(num) };
  }
  if ( rhs.length > 0) {
    return { bound: rhs, numeric: null, modifierText: `${paramId} = ${rhs}` };
  }
  return { bound: null, numeric: 0, modifierText: "0" };
}

export function resolveParamNumber(paramId: string, rawValue: string, model: StudioModel): number {
  const parsed = parseParamBinding(paramId, rawValue);
  if (parsed.numeric !== null) return parsed.numeric;
  if (parsed.bound) {
    const top = model.params.find((p) => p.id === parsed.bound);
    if (top && top.type === "number") return top.default;
  }
  return 0;
}

export async function commitParamValue(
  api: StudioApi,
  componentName: string,
  paramId: string,
  value: number,
): Promise<void> {
  await api.setParam(`${componentName}.${paramId}`, String(value));
}

/**
 * Binds `componentName.paramId` to a top-level Station property `topParamName` (`R = R_load`):
 * 1. Sets/creates the top-level parameter default via `setParam(topParamName, defaultStr)`.
 * 2. Sets the component modifier via `setParam("R1.R", topParamName)`.
 */
export async function bindParamToTopLevel(
  api: StudioApi,
  componentName: string,
  paramId: string,
  topParamName: string,
  currentRawValue: string,
): Promise<boolean> {
  const cleanName = topParamName.trim();
  if (!/^[A-Za-z_]\w*$/.test(cleanName)) return false;
  const numValue = resolveParamNumber(paramId, currentRawValue, api.model);
  await api.setParam(cleanName, String(numValue));
  await api.setParam(`${componentName}.${paramId}`, cleanName);
  return true;
}

export async function unbindParam(
  api: StudioApi,
  componentName: string,
  paramId: string,
  boundName: string,
): Promise<void> {
  const top = api.model.params.find((p) => p.id === boundName);
  const fallback = top && top.type === "number" ? String(top.default) : "0";
  await api.setParam(`${componentName}.${paramId}`, fallback);
}

export type PortAction = {
  portId: string;
  portClass: string;
  kind: "input" | "output";
  buttonLabel: "+ Input" | "+ Probe";
  unit?: string;
  existingEntry?: string;
};

export function portActionFor(
  component: ComponentInst,
  port: IconPort,
  connectors: Connectors,
  entries: StudioModel["entries"],
): PortAction {
  const conn = connectors[port.class];
  const isCausalInput = conn?.kind === "input" || /Input$/.test(port.class);
  const kind: "input" | "output" = isCausalInput ? "input" : "output";
  const buttonLabel = isCausalInput ? "+ Input" : "+ Probe";
  const unit = conn?.potential?.[0]?.unit ?? conn?.flow?.[0]?.unit;
  const target = `${component.name}.${port.id}`;
  const existing = entries.find((e) => e.target === target)?.name;
  return {
    portId: port.id,
    portClass: port.class,
    kind,
    buttonLabel,
    unit,
    existingEntry: existing,
  };
}

export async function exposeComponentPort(
  api: StudioApi,
  component: ComponentInst,
  port: IconPort,
): Promise<void> {
  const action = portActionFor(component, port, api.model.connectors, api.model.entries);
  await api.addEntry(action.kind, `${component.name}.${port.id}`);
}
