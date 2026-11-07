// Reading the model back from omc after every edit: the .mo stays the truth, the canvas is a
// view of what omc says. Pure over an `OmcCalls`, so a test feeds it canned answers.

import type { ComponentInst, Connection, EntryPoint, Placement, StudioModel } from "../model/types.ts";
import type { OmcCalls } from "../omc/omc.ts";
import { atom, list, parseCall, parseValue, type Val } from "../omc/parse.ts";
import { GRID, snap, toPx } from "./exprs.ts";
import { classLook, type IconCache } from "./looks.ts";

const CAUSAL = /^(Real|Integer|Boolean)(Input|Output)$/;
const PRIMITIVE = new Set(["Real", "Integer", "Boolean", "String"]);

export const shortName = (cls: string) => cls.slice(cls.lastIndexOf(".") + 1);
export const quoted = (s: string) => `"${s}"`;

/** `Placement(true,40.0,-20.0,-10,-10,10,10,90.0,…)` → where the plate's centre sits, in px. */
export function placementOf(text: Val | undefined): Placement | null {
  const call = parseCall(text);
  if (!call || call.name !== "Placement") return null;
  const n = (i: number) => Number(call.args[i]);
  if (call.args.length < 8 || !Number.isFinite(n(1)) || !Number.isFinite(n(2))) return null;
  const [x, y] = toPx(n(1), n(2));
  const rot = Number.isFinite(n(7)) ? ((Math.round(n(7) / 90) * 90) % 360 + 360) % 360 : 0;
  return { x: snap(x), y: snap(y), rotation: rot as Placement["rotation"], ...(n(3) > n(5) ? { mirror: true } : {}) };
}

/** A wire's bends from `getNthConnectionAnnotation`: `{Line(true, {0,0}, 0, {{x,y},…}, …)}`, ends dropped. */
export function bendsOf(answer: string): [number, number][] | undefined {
  let v: Val;
  try {
    v = parseValue(answer);
  } catch {
    return undefined;
  }
  const call = parseCall(list(v)[0]);
  if (!call || call.name !== "Line") return undefined;
  const pts = list(parseValue(call.args[3] ?? "{}"));
  const out = pts.slice(1, -1).map((p): [number, number] => {
    const [x, y] = list(p).map((c) => Number(atom(c)));
    return toPx(x, y);
  });
  return out.length ? out.map(([x, y]) => [snap(x), snap(y)]) : undefined;
}

const auto = (i: number): Placement => ({ x: snap(160 + (i % 4) * 200), y: snap(120 + Math.floor(i / 4) * 160), rotation: 0 });

export type Read = { model: StudioModel; looks: IconCache };

export async function readModel(omc: OmcCalls, name: string, file: string | undefined, looks: IconCache): Promise<StudioModel> {
  const comps = list(parseValue(await omc.call(`getComponents(${name})`)));
  const notes = list(parseValue(await omc.call(`getComponentAnnotations(${name})`)));
  const components: ComponentInst[] = [];
  const entries: EntryPoint[] = [];
  const connectors: StudioModel["connectors"] = {};
  const wires: Connection[] = [];
  const nConnections = Number(await omc.call(`getConnectionCount(${name})`)) || 0;
  for (let n = 1; n <= nConnections; n++) {
    const c = list(parseValue(await omc.call(`getNthConnection(${name}, ${n})`)));
    const bends = bendsOf(await omc.call(`getNthConnectionAnnotation(${name}, ${n})`));
    wires.push({ a: atom(c[0]), b: atom(c[1]), ...(bends ? { points: bends } : {}) });
  }

  const typeClass = new Map<string, boolean>();
  let index = 0;
  for (let i = 0; i < comps.length; i++) {
    const c = list(comps[i]);
    const cls = atom(c[0]);
    const id = atom(c[1]);
    if (!cls || !id || atom(c[3]) === "protected") continue;
    // top-level parameters and plain variables are not parts: Real, or a `type` such as
    // Modelica.Units.SI.Inductance, or anything declared parameter/constant
    if (PRIMITIVE.has(cls) || atom(c[8]) === "parameter" || atom(c[8]) === "constant") continue;
    if (!typeClass.has(cls)) typeClass.set(cls, atom(parseValue(await omc.call(`getClassRestriction(${cls})`))) === "type");
    if (typeClass.get(cls)) continue;
    const names = list(parseValue(await omc.call(`getComponentModifierNames(${name}, ${quoted(id)})`)));
    const params: Record<string, string> = {};
    for (const m of names) {
      const key = atom(m);
      params[key] = atom(parseValue(await omc.call(`getComponentModifierValue(${name}, ${id}.${key})`)));
    }
    const look = await classLook(omc, looks, cls, id);
    Object.assign(connectors, look.connectors);
    const placement = placementOf(list(notes[i])[0]) ?? auto(index);
    index++;
    components.push({ name: id, class: cls, params, placement, ...(look.icon ? { icon: look.icon } : {}), ports: look.icon?.ports ?? [] });

    const causal = CAUSAL.exec(shortName(cls));
    if (causal && look.connector) {
      const other = wires.find((w) => w.a === id || w.b === id);
      entries.push({ name: id, kind: causal[2] === "Input" ? "input" : "output", target: other ? (other.a === id ? other.b : other.a) : "" });
    }
  }

  const params = await readParams(omc, name);
  const own = await omc.op<{ icon?: StudioModel["icon"]; connectors?: StudioModel["connectors"] }>("icon", { class: name }).catch(() => ({}) as { icon?: StudioModel["icon"] });
  return { name, ...(file ? { file } : {}), components, connections: wires, params, entries, ...(own.icon ? { icon: own.icon } : {}), connectors };
}

async function readParams(omc: OmcCalls, model: string): Promise<StudioModel["params"]> {
  const out: StudioModel["params"] = [];
  for (const id of list(parseValue(await omc.call(`getParameterNames(${model})`))).map(atom)) {
    const raw = atom(parseValue(await omc.call(`getParameterValue(${model}, ${quoted(id)})`)));
    const v = Number(raw);
    if (raw === "true" || raw === "false") out.push({ id, type: "boolean", default: raw === "true" });
    else if (raw !== "" && Number.isFinite(v)) out.push({ id, type: "number", default: v });
    else out.push({ id, type: "text", default: raw });
  }
  return out;
}

export { GRID };
