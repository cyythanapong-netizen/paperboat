// What a Modelica class looks like on the canvas: its imported icon and connector table, from
// the server's `icon` op, cached per class (library classes never change under Studio).

import type { ConnectorDef, Connectors, IconDef, IconPort } from "@paperboat/shared";
import type { OmcCalls } from "../omc/omc.ts";

export type ClassLook = { icon?: IconDef; connectors: Connectors; connector?: boolean };
export type IconCache = Map<string, Promise<{ icon?: IconDef; connectors: Connectors; connector: boolean }>>;

const short = (cls: string) => cls.slice(cls.lastIndexOf(".") + 1);

/** The built-in table of contracts §4, for a connector class the importer didn't describe. */
export const BUILTIN: Connectors = {
  Pin: { kind: "acausal", handle: "square", size: 8, weight: 1.2 },
  PositivePin: { kind: "acausal", handle: "square", alt: "filled", size: 8, weight: 1.2 },
  NegativePin: { kind: "acausal", handle: "square", size: 8, weight: 1.2 },
  Flange_a: { kind: "acausal", handle: "square", alt: "filled", size: 10, weight: 1.8 },
  Flange_b: { kind: "acausal", handle: "square", size: 10, weight: 1.8 },
  HeatPort_a: { kind: "acausal", handle: "diamond", alt: "filled", size: 8, weight: 1.2, dash: "dashed" },
  HeatPort_b: { kind: "acausal", handle: "diamond", size: 8, weight: 1.2, dash: "dashed" },
  FluidPort_a: { kind: "acausal", handle: "ring2", size: 12, weight: 2 },
  FluidPort_b: { kind: "acausal", handle: "ring2", size: 12, weight: 2 },
  RealInput: { kind: "input", handle: "triangle", size: 8, weight: 1.2 },
  IntegerInput: { kind: "input", handle: "triangle", size: 8, weight: 1.2 },
  BooleanInput: { kind: "input", handle: "triangle", size: 8, weight: 1.2 },
  RealOutput: { kind: "output", handle: "circle", alt: "filled", size: 8, weight: 1.2 },
  IntegerOutput: { kind: "output", handle: "circle", alt: "filled", size: 8, weight: 1.2 },
  BooleanOutput: { kind: "output", handle: "circle", alt: "filled", size: 8, weight: 1.2 },
};

/**
 * The look of class `cls` for an instance called `id`. A connector class (a top-level
 * `RealInput u`) is drawn as its own small plate with one port named after the instance.
 */
export async function classLook(omc: OmcCalls, cache: IconCache, cls: string, id: string): Promise<ClassLook> {
  let entry = cache.get(cls);
  if (!entry) {
    entry = (async () => {
      const got = await omc.op<{ icon?: IconDef; connectors?: Connectors }>("icon", { class: cls }).catch(() => ({}) as { icon?: IconDef; connectors?: Connectors });
      const restriction = (await omc.call(`getClassRestriction(${cls})`).catch(() => "")).replace(/"/g, "");
      return { icon: got.icon, connectors: got.connectors ?? {}, connector: restriction === "connector" };
    })();
    cache.set(cls, entry);
  }
  const { icon, connectors, connector } = await entry;
  const merged: Connectors = { ...connectors };
  const name = short(cls);
  if (!merged[name] && BUILTIN[name]) merged[name] = BUILTIN[name];
  if (connector) {
    const kind = merged[name]?.kind;
    const port: IconPort = { id, class: name, side: kind === "output" ? "left" : "right", at: 50 };
    return { icon: { ...(icon ?? { g: [] }), ports: [port] }, connectors: merged, connector: true };
  }
  // A class without an Icon annotation is drawn as its name, so a part is never an empty plate.
  const drawn: IconDef | undefined = icon && icon.g.length === 0 ? { ...icon, g: [{ t: "text", x: 50, y: 50, s: name }] } : icon;
  return { icon: drawn, connectors: merged };
}

export const connectorOf = (connectors: Connectors, cls: string): ConnectorDef | undefined => connectors[cls];
