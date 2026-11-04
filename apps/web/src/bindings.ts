// Variables (#36) in the UI, the model: what refers to what, renaming, merging the backend's defaults
// with the user's own, and the TOML a developer pastes into a descriptor. The engine
// resolves and checks them (variables.values); nothing here evaluates a formula.

import type { ParamDef, ParamValue, Station, Variable, Variables } from "./protocol";

export type Ref = { var: string } | { expr: string };
export const isRef = (v: unknown): v is Ref => typeof v === "object" && v !== null && ("var" in v || "expr" in v);

/** Names a formula uses (identifiers, built-ins included: only real variables match later). */
export function namesIn(formula: string): string[] {
  return [...formula.matchAll(/(?<![\w.])[A-Za-z_]\w*/g)].map((m) => m[0]);
}

/** Whether `value` refers to variable `name`. */
export function refersTo(value: unknown, name: string): boolean {
  if (!isRef(value)) return false;
  return "var" in value ? value.var === name : namesIn(value.expr).includes(name);
}

/** `formula` with variable `from` renamed `to`. */
export function renameInFormula(formula: string, from: string, to: string): string {
  return formula.replace(/(?<![\w.])[A-Za-z_]\w*/g, (w) => (w === from ? to : w));
}

/** `value` with variable `from` renamed `to`, if it refers to it. */
export function renameRef(value: ParamValue, from: string, to: string): ParamValue {
  if (!isRef(value)) return value;
  return "var" in value ? (value.var === from ? { var: to } : value) : { expr: renameInFormula(value.expr, from, to) };
}

/** `value` with variable `name` replaced by its value `v` (the variable is going away). */
export function inlineRef(value: ParamValue, name: string, v: number | boolean | string): ParamValue {
  if (!isRef(value) || !refersTo(value, name)) return value;
  if ("var" in value) return v;
  const literal = typeof v === "boolean" ? (v ? "1" : "0") : String(v);
  return { expr: value.expr.replace(/(?<![\w.])[A-Za-z_]\w*/g, (w) => (w === name ? `(${literal})` : w)) };
}

/** A place that uses a variable: a block's param on the Flow, a station object's property, or another variable's formula. */
export type Use = { page: "flow" | "station" | "variables"; id: string; field: string; formula?: string };

/** Where each variable is used: blocks' params, station props and other variables' formulas. */
export function usesOf(
  name: string,
  blocks: { id: string; params: Record<string, ParamValue> }[],
  station: Station | null,
  variables: Variables,
): Use[] {
  const out: Use[] = [];
  for (const b of blocks)
    for (const [field, v] of Object.entries(b.params))
      if (refersTo(v, name)) out.push({ page: "flow", id: b.id, field, formula: isRef(v) && "expr" in v ? v.expr : undefined });
  for (const o of station?.objects ?? [])
    for (const [field, v] of Object.entries(o.props ?? {}))
      if (refersTo(v, name)) out.push({ page: "station", id: o.id, field, formula: isRef(v) && "expr" in v ? v.expr : undefined });
  for (const v of variables)
    if (v.expr && v.name !== name && namesIn(v.expr).includes(name)) out.push({ page: "variables", id: v.name, field: "", formula: v.expr });
  return out;
}

/** Whether variable `v` can stand for a param of `def`'s type. */
export function fits(v: Variable, def: ParamDef): boolean {
  switch (def.type) {
    case "number":
      return v.type === "number";
    case "boolean":
      return v.type === "boolean";
    case "enum":
      return v.type === "enum" && def.options.includes(String(v.value));
    case "text":
    case "file":
      return v.type === "text" || v.type === "file";
    default:
      return false; // a key belongs to its block
  }
}

const NAME = /^[A-Za-z_]\w*$/;
const RESERVED = new Set(["t", "pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "exp", "log", "log10",
  "sqrt", "abs", "floor", "ceil", "round", "sign", "frac", "min", "max", "clamp", "mod", "pow"]);

/** Why `name` can't be a variable's name among `list` (except `self`), or "". */
export function badName(name: string, list: Variables, self?: string): string {
  if (!NAME.test(name)) return "letters, digits and _, not starting with a digit";
  if (RESERVED.has(name)) return `${name} is a formula's own name`;
  if (name !== self && list.some((v) => v.name === name)) return `there's already a ${name}`;
  return "";
}

/** `base`, or `base_2`, `base_3`… : a name no variable in `list` has. */
export function freeName(base: string, list: Variables): string {
  const clean = /^[A-Za-z_]/.test(base) ? base.replace(/\W/g, "_") : `v_${base.replace(/\W/g, "_")}`;
  const start = RESERVED.has(clean) ? `${clean}_1` : clean;
  if (!list.some((v) => v.name === start)) return start;
  let i = 2;
  while (list.some((v) => v.name === `${start}_${i}`)) i++;
  return `${start}_${i}`;
}

/**
 * The variables in use: the user's own (their order), then the backend's defaults they
 * don't mention. A default the user never touched is the backend's, so a newer backend's
 * new default shows up; deleting a default brings it back (Reset).
 */
export function merged(defaults: Variables, mine: Variables | undefined): Variables {
  if (!mine) return defaults;
  const named = new Set(mine.map((v) => v.name));
  return [...mine, ...defaults.filter((v) => !named.has(v.name))];
}

/** The default value of a new variable of `type`. */
export function emptyOf(type: Variable["type"], options?: string[]): Variable["value"] {
  return type === "number" ? 0 : type === "boolean" ? false : type === "enum" ? (options?.[0] ?? "a") : "";
}

const tomlString = (s: string) => JSON.stringify(s);

/** The list as a descriptor's [[variables]] tables, to paste and make them a backend's defaults. */
export function toToml(list: Variables): string {
  return list
    .map((v) => {
      const lines = ["[[variables]]", `name = ${tomlString(v.name)}`, `type = ${tomlString(v.type)}`];
      if (v.expr !== undefined) lines.push(`expr = ${tomlString(v.expr)}`);
      else if (v.value !== undefined) lines.push(`value = ${typeof v.value === "string" ? tomlString(v.value) : String(v.value)}`);
      for (const k of ["unit", "note", "group"] as const) if (v[k]) lines.push(`${k} = ${tomlString(v[k]!)}`);
      for (const k of ["min", "max", "step"] as const) if (v[k] !== undefined) lines.push(`${k} = ${v[k]}`);
      if (v.whole) lines.push("whole = true");
      if (v.options) lines.push(`options = [${v.options.map(tomlString).join(", ")}]`);
      return lines.join("\n");
    })
    .join("\n\n");
}

/** The user's variables for a backend, kept in this browser until projects (#34). */
export function loadMine(engine: string): Variables | undefined {
  try {
    const v = JSON.parse(localStorage.getItem(`paperboat.variables:${engine}`) ?? "null");
    return Array.isArray(v) ? (v as Variables) : undefined;
  } catch {
    return undefined;
  }
}
export function saveMine(engine: string, list: Variables | undefined) {
  try {
    if (list) localStorage.setItem(`paperboat.variables:${engine}`, JSON.stringify(list));
    else localStorage.removeItem(`paperboat.variables:${engine}`);
  } catch {
    /* storage blocked: they last for the session */
  }
}
