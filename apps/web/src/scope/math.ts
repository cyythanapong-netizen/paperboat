/**
 * The Scope's measurements (#37), pure functions over sample arrays: trigger search, FFT,
 * stats and CSV. Imports nothing from the app.
 */

type Samples = ArrayLike<number>;

/**
 * The time of the last `edge` crossing of `level` in (t, v) at or before `before` (default:
 * the end), or null. Hysteresis: the signal must first be `hyst` past the level on the other
 * side, so noise around the level doesn't re-trigger.
 */
export function findTrigger(t: Samples, v: Samples, level: number, edge: "rising" | "falling" = "rising", before = Infinity, hyst = 0): number | null {
  const up = edge === "rising";
  let armedAt = -1;
  let found: number | null = null;
  for (let i = 0; i < t.length && t[i] <= before; i++) {
    const below = up ? v[i] < level - hyst : v[i] > level + hyst;
    if (below) armedAt = i;
    else if (armedAt >= 0 && i > 0 && (up ? v[i] >= level : v[i] <= level)) {
      // Interpolate where it crossed between i−1 and i.
      const a = v[i - 1], b = v[i];
      const f = b === a ? 0 : (level - a) / (b - a);
      found = t[i - 1] + Math.max(0, Math.min(1, f)) * (t[i] - t[i - 1]);
      armedAt = -1;
    }
  }
  return found;
}

/**
 * The magnitude spectrum of evenly spaced samples `v` (spacing `dt` s), Hann-windowed, in dB
 * (0 dB: a full-scale sine of amplitude 1). Uses the last power-of-two count of samples.
 * Returns frequencies (Hz) from the first bin up, and their dB.
 */
export function spectrum(v: Samples, dt: number): { f: number[]; db: number[] } {
  let n = 1;
  while (n * 2 <= v.length) n *= 2;
  if (n < 4 || !(dt > 0)) return { f: [], db: [] };
  const re = new Float64Array(n), im = new Float64Array(n);
  const start = v.length - n;
  let wsum = 0;
  for (let i = 0; i < n; i++) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
    re[i] = v[start + i] * w;
    wsum += w;
  }
  fft(re, im);
  const f: number[] = [], db: number[] = [];
  for (let k = 1; k <= n / 2; k++) {
    const mag = (2 * Math.hypot(re[k], im[k])) / wsum;
    f.push(k / (n * dt));
    db.push(20 * Math.log10(Math.max(mag, 1e-12)));
  }
  return { f, db };
}

/** In-place radix-2 FFT; `re.length` must be a power of two. */
export function fft(re: Float64Array, im: Float64Array): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr;
        im[b] = im[a] - xi;
        re[a] += xr;
        im[a] += xi;
        [cr, ci] = [cr * wr - ci * wi, cr * wi + ci * wr];
      }
    }
  }
}

export type Stats = { min: number; max: number; mean: number; rms: number; n: number };

/** Min, max, mean and RMS of the samples whose time is in [t0, t1] (all by default). */
export function stats(t: Samples, v: Samples, t0 = -Infinity, t1 = Infinity): Stats {
  let min = Infinity, max = -Infinity, sum = 0, sq = 0, n = 0;
  for (let i = 0; i < v.length; i++) {
    if (t[i] < t0 || t[i] > t1) continue;
    min = Math.min(min, v[i]);
    max = Math.max(max, v[i]);
    sum += v[i];
    sq += v[i] * v[i];
    n++;
  }
  return n ? { min, max, mean: sum / n, rms: Math.sqrt(sq / n), n } : { min: NaN, max: NaN, mean: NaN, rms: NaN, n: 0 };
}

/** The value of (t, v) at time `at`, linearly between samples; null outside them. */
export function valueAt(t: Samples, v: Samples, at: number): number | null {
  if (!t.length || at < t[0] || at > t[t.length - 1]) return null;
  let lo = 0, hi = t.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (t[mid] <= at) lo = mid;
    else hi = mid;
  }
  return t[hi] === t[lo] ? v[lo] : v[lo] + ((v[hi] - v[lo]) * (at - t[lo])) / (t[hi] - t[lo]);
}

/** Channels as CSV: a `t` column and one per channel, rows where any channel has a sample in [t0, t1]. */
export function toCsv(channels: { label: string; unit?: string; t: Samples; v: Samples }[], t0 = -Infinity, t1 = Infinity): string {
  const rows = new Map<number, (number | undefined)[]>();
  channels.forEach((c, k) => {
    for (let i = 0; i < c.t.length; i++) {
      if (c.t[i] < t0 || c.t[i] > t1) continue;
      let row = rows.get(c.t[i]);
      if (!row) rows.set(c.t[i], (row = new Array(channels.length)));
      row[k] = c.v[i];
    }
  });
  const quote = (s: string) => (/[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
  const head = ["t (s)", ...channels.map((c) => quote(c.unit ? `${c.label} (${c.unit})` : c.label))].join(",");
  const body = [...rows.entries()].sort((a, b) => a[0] - b[0]).map(([t, row]) => [t, ...Array.from(row, (x) => x ?? "")].join(","));
  return [head, ...body].join("\n") + "\n";
}
