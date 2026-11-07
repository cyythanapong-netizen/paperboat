/**
 * The trace store (#37): every plotted channel's history, in typed arrays outside React
 * state. Append is O(1); a buffer that fills keeps every other sample (and the latest), so a
 * long run keeps its whole span at half the resolution, as the old `append` did. Readers
 * re-read on `version`, which the app ticks at most once per animation frame.
 *
 * Self-contained on purpose: nothing here imports the app (engine, protocol, layout), so the
 * Scope can move into a shared kit (#160) unchanged.
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

  private channel(node: string, id: string, label: string, unit?: string): Channel {
    let list = this.byNode.get(node);
    if (!list) this.byNode.set(node, (list = []));
    let c = list.find((x) => x.id === id);
    if (!c) list.push((c = { id, label, unit, buf: new TraceBuffer() }));
    return c;
  }
}
