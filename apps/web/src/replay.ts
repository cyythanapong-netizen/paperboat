// Replay (#284): what a finished run looked like at any time, and moving a playhead through it.
//
// The samples are already in the trace store (traces.ts): replay reads them at t instead of at
// their end. Station changes arrive as scene.patch messages; this keeps them with the run clock,
// so the scene can be put back at t too. Records let a run be saved and opened again later.

import type { Entity, EntityPatch } from "./protocol";
import type { Channel, TraceStore } from "./traces";

/** The playhead: where a replay is, whether it runs, and how fast. */
export type ReplayState = {
  active: boolean;
  playing: boolean;
  /** Sim time, in the run's clock. */
  time: number;
  /** The run's span (the trace store's). */
  from: number;
  to: number;
  /** Playback speed: sim seconds per wall second. */
  speed: number;
};

/** The speeds the dock offers, as RunOptions does for live pacing. */
export const REPLAY_SPEEDS = [0.25, 0.5, 1, 2, 5] as const;

/** One frame of station changes, at the run clock when it arrived. */
export type EntityFrame = { t: number; entities: EntityPatch[] };

/**
 * Station history for a run (#284): the scene as it was when the run started, then each patch at
 * the time it arrived. `entitiesAt` rebuilds the scene at any point, so scrubbing moves the
 * Station back as well as the scopes.
 */
export class ReplayStore {
  base: Record<string, Entity> = {};
  frames: EntityFrame[] = [];

  /** A new run starts from the scene on screen now. */
  reset(base: Record<string, Entity> = {}): void {
    this.base = { ...base };
    this.frames = [];
  }

  clear(): void {
    this.base = {};
    this.frames = [];
  }

  /** A scene.patch during a run: its entities at the run clock (`t`). */
  add(t: number, entities: EntityPatch[]): void {
    if (!entities.length || !Number.isFinite(t)) return;
    // Patches are sent in clock order; a late batch still goes after what it follows.
    const last = this.frames[this.frames.length - 1];
    if (last && last.t === t) last.entities.push(...entities);
    else this.frames.push({ t, entities: [...entities] });
  }

  /** The scene at `t`: the run's base, with every patch up to t applied. */
  entitiesAt(t: number): Record<string, Entity> {
    const out: Record<string, Entity> = { ...this.base };
    for (const frame of this.frames) {
      if (frame.t > t) break;
      for (const p of frame.entities) {
        const before = out[p.id];
        if (before) out[p.id] = { ...before, ...p };
        else if (p.shape && p.pos) out[p.id] = p as Entity;
      }
    }
    return out;
  }

  /** Whether this run moved the station at all: without a patch, replaying the scene is pointless. */
  get moved(): boolean {
    return this.frames.length > 0;
  }
}

/** A scalar or vector value as a wire probe or readout writes it. */
export function formatProbe(v: number | number[] | unknown): string {
  const one = (x: number) => (Number.isFinite(x) ? (Math.abs(x) >= 1e4 || (x !== 0 && Math.abs(x) < 1e-3) ? x.toExponential(2) : x.toFixed(3)) : "–");
  if (typeof v === "number") return one(v);
  if (Array.isArray(v)) return `[${v.map((x) => (typeof x === "number" ? one(x) : String(x))).join(", ")}]`;
  return v === undefined ? "" : String(v);
}

/** A `signal` sample as a record keeps it (protocol shape, `type` implied): a number, text or a vector. */
export type RecordSignal = { node: string; port?: string; t: number; v: number | string | number[]; components?: { id: string; unit?: string }[] };

/** A channel as a record keeps it: lossless, including a vector's component names. */
export type RecordChannel = { node: string; id: string; label: string; unit?: string; t: number[]; v: number[] };

/** One saved replay (see the `record.json` proposal in #277; replay only needs signals and scene). */
export type ReplayRecord = {
  paperboat: number;
  engine?: string;
  run?: { id?: string; status?: string; started?: number };
  /** Channels from a Paperboat replay export. */
  channels?: RecordChannel[];
  /** Signals from another writer (a run record): each may carry its own `type`. */
  signals?: RecordSignal[];
  /** The station at the run start, and its changes over the run. */
  scene?: { base?: Record<string, Entity>; frames?: EntityFrame[] };
};

/** A named error for a record that cannot be replayed (as #277 asks: a missing or invalid record fails by name). */
export class RecordError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "RecordError";
  }
}

/** Every channel of every monitor, as a record keeps them. */
export function channelsOf(traces: TraceStore): RecordChannel[] {
  const out: RecordChannel[] = [];
  for (const node of traces.nodes())
    for (const c of traces.channels(node) as readonly Channel[])
      out.push({ node, id: c.id, label: c.label, ...(c.unit ? { unit: c.unit } : {}), t: Array.from(c.buf.times), v: Array.from(c.buf.values) });
  return out;
}

/** A run's samples and station history as one JSON record. */
export function recordOf(engine: string, id: string | undefined, status: string | undefined, started: number | undefined, traces: TraceStore, scene: ReplayStore): ReplayRecord {
  return {
    paperboat: 0,
    ...(engine ? { engine } : {}),
    run: { ...(id ? { id } : {}), ...(status ? { status } : {}), ...(started ? { started } : {}) },
    channels: channelsOf(traces),
    ...(scene.moved || Object.keys(scene.base).length ? { scene: { base: scene.base, frames: scene.frames } } : {}),
  };
}

const isNumber = (x: unknown): x is number => typeof x === "number" && Number.isFinite(x);
const isSignalValue = (x: unknown): x is number | string | number[] => isNumber(x) || typeof x === "string" || (Array.isArray(x) && x.every(isNumber));

/** Parse a record file, with a named error for what makes it unreplayable. */
export function parseRecord(text: string): ReplayRecord {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    throw new RecordError("the record is not JSON");
  }
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) throw new RecordError("the record is not an object");
  const r = raw as Record<string, unknown>;
  if (r.paperboat !== undefined && r.paperboat !== 0) throw new RecordError(`unknown record version ${String(r.paperboat)}`);

  const record: ReplayRecord = { paperboat: 0, ...(typeof r.engine === "string" ? { engine: r.engine } : {}) };
  if (r.run && typeof r.run === "object") {
    const run = r.run as Record<string, unknown>;
    record.run = {
      ...(typeof run.id === "string" ? { id: run.id } : {}),
      ...(typeof run.status === "string" ? { status: run.status } : {}),
      ...(isNumber(run.started) ? { started: run.started } : {}),
    };
  }

  if (Array.isArray(r.channels)) {
    record.channels = r.channels.map((item, i) => {
      const c = item as Record<string, unknown>;
      if (!c || typeof c !== "object" || typeof c.node !== "string" || typeof c.id !== "string") throw new RecordError(`channel ${i + 1} has no node and id`);
      if (!Array.isArray(c.t) || !Array.isArray(c.v) || c.t.length !== c.v.length) throw new RecordError(`channel ${c.node}.${c.id} has no matching t and v`);
      const t = c.t.filter(isNumber);
      const v = c.v.filter(isNumber);
      if (t.length !== c.t.length || v.length !== c.v.length) throw new RecordError(`channel ${c.node}.${c.id} has a non-numeric sample`);
      return { node: c.node, id: c.id, label: typeof c.label === "string" ? c.label : c.id, ...(typeof c.unit === "string" ? { unit: c.unit } : {}), t, v };
    });
  }

  if (Array.isArray(r.signals)) {
    record.signals = r.signals.map((item, i) => {
      const s = item as Record<string, unknown>;
      if (!s || typeof s !== "object" || typeof s.node !== "string" || !isNumber(s.t) || !isSignalValue(s.v)) throw new RecordError(`signal ${i + 1} is not a signal sample`);
      return {
        node: s.node,
        ...(typeof s.port === "string" ? { port: s.port } : {}),
        t: s.t,
        v: s.v,
        ...(Array.isArray(s.components)
          ? {
              components: s.components.map((c: unknown, index: number) => {
                const one = (c && typeof c === "object" ? c : {}) as { id?: unknown; unit?: unknown };
                return { id: typeof one.id === "string" ? one.id : String(index), ...(typeof one.unit === "string" ? { unit: one.unit } : {}) };
              }),
            }
          : {}),
      };
    });
  }

  if (!record.channels?.length && !record.signals?.length) throw new RecordError("the record has no signals to replay");

  if (r.scene && typeof r.scene === "object") {
    const scene = r.scene as Record<string, unknown>;
    const base = scene.base && typeof scene.base === "object" && !Array.isArray(scene.base) ? (scene.base as Record<string, Entity>) : {};
    const frames: EntityFrame[] = [];
    if (Array.isArray(scene.frames))
      for (const [i, item] of scene.frames.entries()) {
        const f = item as Record<string, unknown>;
        if (!f || !isNumber(f.t) || !Array.isArray(f.entities)) throw new RecordError(`scene frame ${i + 1} has no t and entities`);
        frames.push({ t: f.t, entities: f.entities as EntityPatch[] });
      }
    record.scene = { ...(Object.keys(base).length ? { base } : {}), ...(frames.length ? { frames } : {}) };
  }
  return record;
}
