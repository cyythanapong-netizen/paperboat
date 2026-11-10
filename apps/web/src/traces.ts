/**
 * The trace store (#37): every plotted channel's history, in typed arrays outside React
 * state. Append is O(1); a buffer that fills keeps every other sample (and the latest), so a
 * long run keeps its whole span at half the resolution, as the old `append` did. Readers
 * re-read on `version`, which the app ticks at most once per animation frame.
 *
 * A host service: the app keeps the history of every monitor's signals and hands an instrument
 * its channels (`state.traces`); a toolkit's instruments (a graph, a scope) only draw them.
 * Self-contained on purpose: nothing here imports the app (engine, protocol, layout).
 */

/** Samples kept per channel before it thins. */
export const MAX_POINTS = 20000;

export class TraceBuffer {
  t: Float64Array;
  v: Float64Array;
  n = 0;
  readonly cap: number;
  constructor(cap = MAX_POINTS) {
    this.cap = cap;
    this.t = new Float64Array(cap);
    this.v = new Float64Array(cap);
  }
  push(t: number, v: number): void {
    if (this.n === this.cap) this.thin();
    this.t[this.n] = t;
    this.v[this.n] = v;
    this.n++;
  }
  /** Keep every other sample and the latest: the span stays, the resolution halves. */
  private thin(): void {
    let k = 0;
    for (let i = 0; i < this.n; i += 2, k++) {
      this.t[k] = this.t[i];
      this.v[k] = this.v[i];
    }
    if ((this.n - 1) % 2 !== 0) {
      this.t[k] = this.t[this.n - 1];
      this.v[k] = this.v[this.n - 1];
      k++;
    }
    this.n = k;
  }
  /** The samples, as views (no copy). */
  get times(): Float64Array { return this.t.subarray(0, this.n); }
  get values(): Float64Array { return this.v.subarray(0, this.n); }
  get last(): number | undefined { return this.n ? this.v[this.n - 1] : undefined; }
}

/** One plotted line: an input of a monitor, or one component of a vector on it. */
export type Channel = {
  /** Unique within its monitor: the input ("a"), or input.component ("c.theta"); "in" for one-input monitors. */
  id: string;
  /** What the legend says: the component's name, else the input's. */
  label: string;
  unit?: string;
  buf: TraceBuffer;
};

/** The index of the latest sample at or before `t`: what a replay shows between samples. -1: before the first. */
function sampleAt(times: Float64Array, n: number, t: number): number {
  if (!n || t < times[0]) return -1;
  let lo = 0;
  let hi = n - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (times[mid] <= t) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

/** What the store takes: the shape of a protocol `signal` message. */
export type Sample = { node: string; port?: string; t: number; v: unknown; components?: { id: string; unit?: string }[] };

export class TraceStore {
  private byNode = new Map<string, Channel[]>();
  version = 0;

  clear(): void {
    this.byNode.clear();
    this.version++;
  }

  /** A number joins its input's channel; a vector one channel per component. Text is not plotted. */
  add(s: Sample): void {
    const port = s.port ?? "in";
    if (typeof s.v === "number") this.channel(s.node, port, port).buf.push(s.t, s.v);
    else if (Array.isArray(s.v))
      s.v.forEach((x, i) => {
        const c = s.components?.[i];
        const name = c?.id ?? String(i);
        if (typeof x === "number") this.channel(s.node, `${port}.${name}`, port === "in" ? name : `${port}.${name}`, c?.unit).buf.push(s.t, x);
      });
  }

  /** A monitor's channels, in the order they first arrived. */
  channels(node: string): readonly Channel[] {
    return this.byNode.get(node) ?? [];
  }

  /** Replace every channel with a saved record's (a replay of a run that is not this session's). */
  load(channels: readonly { node: string; id: string; label: string; unit?: string; t: readonly number[]; v: readonly number[] }[]): void {
    this.clear();
    for (const c of channels) {
      const channel = this.channel(c.node, c.id, c.label, c.unit);
      for (let i = 0; i < c.t.length && i < c.v.length; i++) channel.buf.push(c.t[i], c.v[i]);
    }
  }

  /** The monitors that have ever sent a sample. */
  nodes(): string[] {
    return [...this.byNode.keys()];
  }

  /** The span every channel covers: the first and last sample times, or null when nothing was sampled. */
  span(): [number, number] | null {
    let t0 = Infinity;
    let t1 = -Infinity;
    for (const list of this.byNode.values())
      for (const c of list) {
        if (!c.buf.n) continue;
        t0 = Math.min(t0, c.buf.t[0]);
        t1 = Math.max(t1, c.buf.t[c.buf.n - 1]);
      }
    return Number.isFinite(t0) && Number.isFinite(t1) ? [t0, t1] : null;
  }

  /** The run's sample spacing: the middle of the gaps in its channels, else `fallback` (the backend's dt). */
  step(fallback = 0.01): number {
    const gaps: number[] = [];
    for (const list of this.byNode.values())
      for (const c of list) {
        if (c.buf.n < 2) continue;
        const start = Math.max(1, c.buf.n - 200);
        for (let i = start; i < c.buf.n; i++) {
          const d = c.buf.t[i] - c.buf.t[i - 1];
          if (d > 0 && Number.isFinite(d)) gaps.push(d);
        }
        if (gaps.length) {
          gaps.sort((a, b) => a - b);
          return gaps[Math.floor(gaps.length / 2)];
        }
      }
    return fallback > 0 ? fallback : 0.01;
  }

  /**
   * What a monitor showed at `t`: each input's latest sample at or before it, as a scalar or, for
   * a vector input, its components in order. Undefined before the first sample and for an unknown
   * monitor. Text samples were never plotted, so they are not here (readers fall back to `latest`).
   */
  valuesAt(node: string, t: number): Record<string, number | number[]> | undefined {
    const list = this.byNode.get(node);
    if (!list?.length) return undefined;
    const out: Record<string, number | number[]> = {};
    const vectors = new Map<string, number[]>();
    for (const c of list) {
      if (!c.buf.n) continue;
      const i = sampleAt(c.buf.t, c.buf.n, t);
      if (i < 0) continue;
      const dot = c.id.indexOf(".");
      if (dot < 0) out[c.id] = c.buf.v[i];
      else {
        const port = c.id.slice(0, dot);
        let parts = vectors.get(port);
        if (!parts) vectors.set(port, (parts = []));
        parts.push(c.buf.v[i]);
      }
    }
    for (const [port, parts] of vectors) out[port] = parts;
    return Object.keys(out).length ? out : undefined;
  }

  private channel(node: string, id: string, label: string, unit?: string): Channel {
    let list = this.byNode.get(node);
    if (!list) this.byNode.set(node, (list = []));
    let c = list.find((x) => x.id === id);
    if (!c) list.push((c = { id, label, unit, buf: new TraceBuffer() }));
    return c;
  }
}
