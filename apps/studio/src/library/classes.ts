// The class library behind the tree and `/` quick-add: Modelica's packages, read lazily from
// omc one package at a time (`getClassNames(P)`), and one flat list of every part for searching.

import type { OmcCalls } from "../omc/omc.ts";
import { atom, list, parseValue } from "../omc/parse.ts";

export type Kind = "package" | "part";
export type Entry = { name: string; full: string; kind: Kind; comment: string };

/** Packages that hold documentation and demos, not parts. */
const SKIP = new Set(["UsersGuide", "Examples", "Icons", "Internal", "Types", "Interfaces", "Utilities"]);

export const isPartPath = (full: string) => !full.split(".").some((s, i) => i > 0 && SKIP.has(s));

/** The children of package `pkg`: packages to open, and models, blocks and connectors to place. */
const seen = new Map<string, Promise<Entry[]>>();

export function children(omc: OmcCalls, pkg: string): Promise<Entry[]> {
  let got = seen.get(pkg);
  if (!got) {
    got = read(omc, pkg);
    seen.set(pkg, got);
    got.catch(() => seen.delete(pkg));
  }
  return got;
}

async function read(omc: OmcCalls, pkg: string): Promise<Entry[]> {
  const names = list(parseValue(await omc.call(`getClassNames(${pkg})`))).map(atom);
  const out = await Promise.all(
    names
      .filter((n) => n && !(SKIP.has(n) && pkg === "Modelica") && n !== "UsersGuide" && n !== "Examples")
      .map(async (name): Promise<Entry | null> => {
        const full = `${pkg}.${name}`;
        const restriction = atom(parseValue(await omc.call(`getClassRestriction(${full})`).catch(() => '""')));
        if (restriction === "package") return { name, full, kind: "package", comment: "" };
        if (!["model", "block", "connector", "class"].includes(restriction)) return null;
        if (atom(parseValue(await omc.call(`isPartial(${full})`).catch(() => "false"))) === "true") return null;
        const comment = atom(parseValue(await omc.call(`getClassComment(${full})`).catch(() => '""')));
        return { name, full, kind: "part", comment };
      }),
  );
  return out.filter((e): e is Entry => e !== null);
}

/**
 * Every part under `pkg`, qualified, for searching. A name that other names are inside of is a
 * package, so it is left out without asking omc about each of thousands; so are demos and guides.
 */
export async function flat(omc: OmcCalls, pkg: string): Promise<string[]> {
  const all = list(parseValue(await omc.call(`getClassNames(${pkg}, recursive=true)`))).map(atom);
  const packages = new Set(all.flatMap((n) => n.split(".").slice(1).map((_, i, parts) => n.split(".").slice(0, parts.length - i).join("."))));
  return all.filter((n) => n !== pkg && !packages.has(n) && isPartPath(n) && /^[A-Z]/.test(n.slice(n.lastIndexOf(".") + 1)));
}

/** Matches of `query` among `names` by short name first, then by path: "res" finds Resistor. */
export function search(names: string[], query: string, limit = 40): string[] {
  const q = query.trim().toLowerCase();
  if (!q) return [];
  const short = (n: string) => n.slice(n.lastIndexOf(".") + 1).toLowerCase();
  const rank = (n: string) => (short(n) === q ? 0 : short(n).startsWith(q) ? 1 : short(n).includes(q) ? 2 : n.toLowerCase().includes(q) ? 3 : 4);
  return names
    .map((n) => [rank(n), n] as const)
    .filter(([r]) => r < 4)
    .sort((a, b) => a[0] - b[0] || a[1].length - b[1].length)
    .slice(0, limit)
    .map(([, n]) => n);
}
