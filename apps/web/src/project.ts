// Project files (#82, #34): everything a user made, as one JSON file (`name.ppj`). Pure functions: building the
// file, reading and checking one, naming it, and the small lists the browser keeps (recent
// projects and the autosave). The shape is `Project` in protocol.ts, generated from the schema.

import type { DashboardLayout, Graph, Project, RunSettings, Station, Variables } from "./protocol";
import { readStored as read, writeStored as write } from "./storage.ts";

/** The format version this app writes and reads. A newer file is refused. */
export const PROJECT_VERSION = 0;
/** The file name's ending. One place to change it. */
export const PROJECT_EXT = ".ppj";

type Parts = {
  backend: string;
  flow: Graph;
  station?: Station | null;
  variables?: Variables;
  dashboard?: DashboardLayout;
  run?: RunSettings;
};

/** The project for these parts: the parts that are empty or absent are left out, so the file stays small and readable. */
export function buildProject({ backend, flow, station, variables, dashboard, run }: Parts): Project {
  return {
    paperboat: PROJECT_VERSION,
    backend,
    flow,
    ...(station ? { station } : {}),
    ...(variables && variables.length > 0 ? { variables } : {}),
    ...(dashboard ? { dashboard } : {}),
    ...(run && Object.keys(run).length > 0 ? { run } : {}),
  };
}

/** A project's text: indented, so a diff shows what changed. */
export const stringify = (p: Project) => `${JSON.stringify(p, null, 2)}\n`;

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const isText = (v: unknown): v is string => typeof v === "string";
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const pair = (v: unknown): v is [string, string] => Array.isArray(v) && v.length === 2 && v.every(isText);

const VARIABLE_TYPES = ["number", "boolean", "enum", "text", "file"];
const GROUP_KINDS = ["visual", "subsystem", "story"];

/** The first thing wrong with the flow, in words, or null. */
function flowProblem(flow: unknown): string | null {
  if (!isObj(flow)) return "flow must be an object";
  if (!Array.isArray(flow.nodes)) return "flow.nodes must be a list";
  if (!Array.isArray(flow.edges)) return "flow.edges must be a list";
  for (const [i, n] of flow.nodes.entries()) {
    if (!isObj(n) || !isText(n.id) || !isText(n.type)) return `flow.nodes[${i}] needs an id and a type`;
    if (n.params !== undefined && !isObj(n.params)) return `flow.nodes[${i}].params must be an object`;
    if (n.pos !== undefined && !(Array.isArray(n.pos) && n.pos.length === 2 && n.pos.every(isNum))) return `flow.nodes[${i}].pos must be two numbers`;
  }
  for (const [i, e] of flow.edges.entries()) {
    if (!isObj(e) || !pair(e.from) || !pair(e.to)) return `flow.edges[${i}] needs from and to as [block, port]`;
  }
  if (flow.groups !== undefined) {
    if (!Array.isArray(flow.groups)) return "flow.groups must be a list";
    for (const [i, g] of flow.groups.entries()) {
      if (!isObj(g) || !isText(g.id) || !GROUP_KINDS.includes(g.kind as string) || !Array.isArray(g.nodes)) return `flow.groups[${i}] needs an id, a kind and its nodes`;
    }
  }
  return null;
}

/** The first thing wrong with the optional parts, in words, or null. */
function partsProblem(p: Record<string, unknown>): string | null {
  const { station, variables, dashboard, run } = p;
  if (station !== undefined) {
    if (!isObj(station) || !Array.isArray(station.objects)) return "station.objects must be a list";
    for (const [i, o] of station.objects.entries()) if (!isObj(o) || !isText(o.id) || !isText(o.type)) return `station.objects[${i}] needs an id and a type`;
  }
  if (variables !== undefined) {
    if (!Array.isArray(variables)) return "variables must be a list";
    for (const [i, v] of variables.entries()) {
      if (!isObj(v) || !isText(v.name) || !VARIABLE_TYPES.includes(v.type as string)) return `variables[${i}] needs a name and a type`;
    }
  }
  if (dashboard !== undefined && !(isObj(dashboard) && Array.isArray(dashboard.instruments))) return "dashboard.instruments must be a list";
  if (run !== undefined) {
    if (!isObj(run)) return "run must be an object";
    for (const k of ["duration", "dt", "speed"]) if (run[k] !== undefined && !isNum(run[k])) return `run.${k} must be a number`;
  }
  return null;
}

/**
 * Read a project file's text. Either the project or why it can't be opened, in words for the
 * user; nothing is changed by reading. A newer format is refused, and says so.
 */
export function parseProject(text: string): { project: Project } | { error: string } {
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch {
    return { error: "This isn't a Paperboat project: the file is not valid JSON." };
  }
  if (!isObj(data) || data.paperboat === undefined) return { error: 'This isn\'t a Paperboat project: it has no "paperboat" version.' };
  const v = data.paperboat;
  if (!(typeof v === "number" && Number.isInteger(v) && v >= 0)) return { error: 'The project\'s "paperboat" version must be a whole number.' };
  if (v > PROJECT_VERSION) return { error: `This project is format ${v}, made by a newer Paperboat. This one reads format ${PROJECT_VERSION}: update it to open this project.` };
  if (!isText(data.backend)) return { error: 'The project must say which backend it was made on ("backend").' };
  const problem = flowProblem(data.flow) ?? partsProblem(data);
  if (problem) return { error: `The project is damaged: ${problem}.` };
  return { project: data as unknown as Project };
}

/** A file name for a project: the name with what a file system dislikes made into dashes. */
export function projectFileName(name: string): string {
  const clean = name.replace(/[\\/:*?"<>|\u0000-\u001f]/g, "-").trim().replace(/^\.+/, "");
  return `${clean || "project"}${PROJECT_EXT}`;
}

/** The project's name from its file's: without `.ppj` (or `.paperboat.json`, `.json` from before). */
export function projectName(file: string): string {
  const name = file.replace(/\.(ppj|paperboat\.json|json)$/i, "").trim();
  return name || "Untitled";
}

/** "just now", "5 min ago", "2 h ago", "3 d ago". */
export function ago(then: number, now: number): string {
  const s = Math.max(0, Math.round((now - then) / 1000));
  if (s < 60) return "just now";
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return `${Math.round(s / 86400)} d ago`;
}

// ---- What the browser keeps: recent projects (their content, so they can be reopened) and the autosave ----

/** A project kept in this browser, with the name it was saved under and when. */
export type Stored = { name: string; savedAt: number; project: Project };

export const RECENT_MAX = 5;
/** All recent projects together stay under this many characters (localStorage holds about 5 MB). */
export const RECENT_CHARS = 1_500_000;

/** `entry` first, a project of the same name replaced, the list cut to `RECENT_MAX` and `RECENT_CHARS` (oldest go first). */
export function addRecent(list: Stored[], entry: Stored): Stored[] {
  const out: Stored[] = [];
  let size = 0;
  for (const s of [entry, ...list.filter((x) => x.name !== entry.name)]) {
    const n = JSON.stringify(s).length;
    if (out.length >= RECENT_MAX || (out.length > 0 && size + n > RECENT_CHARS)) break;
    out.push(s);
    size += n;
  }
  return out;
}

const RECENT_KEY = "paperboat.recent";
const autosaveKey = (engine: string) => `paperboat.autosave:${engine}`;

const isStored = (v: unknown): v is Stored => isObj(v) && isText(v.name) && isNum(v.savedAt) && isObj(v.project) && "flow" in v.project;

export const loadRecent = (): Stored[] => {
  const v = read(RECENT_KEY, (x): x is unknown[] => Array.isArray(x));
  return (v ?? []).filter(isStored);
};
export const saveRecent = (list: Stored[]) => write(RECENT_KEY, list);

/** The unsaved work of this backend's last session, kept as it happens. */
export const loadAutosave = (engine: string): Stored | undefined => read(autosaveKey(engine), isStored);
export const saveAutosave = (engine: string, s: Stored) => write(autosaveKey(engine), s);
export const clearAutosave = (engine: string) => write(autosaveKey(engine), undefined);
