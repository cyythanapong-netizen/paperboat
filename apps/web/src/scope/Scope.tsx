/**
 * The Scope (#37): the study instrument. Several channels over time on a canvas (uPlot), one
 * y axis per unit, zoom and pan, live or frozen, two cursors with deltas, an edge trigger,
 * xy and fft modes, four draw styles, CSV and PNG export.
 *
 * The screen is output only; every control sits in the grey caption under it. Settings
 * (mode, window, trigger, draw, hidden, ylog) are the caller's to keep (the Board layout);
 * zoom, cursors and freeze are this view's own and are not saved.
 *
 * Imports only React, uPlot and its siblings, never the app (tested), so it can move into a
 * shared kit for Paperboat Studio (#160) and render a finished result with live={false}.
 */
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import type { KeyboardEvent as ReactKeyboardEvent, PointerEvent as ReactPointerEvent } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { findTrigger, spectrum, toCsv, valueAt } from "./math";
import type { Channel } from "./trace";

export type ScopeMode = "time" | "xy" | "fft";
export type DrawStyle = "line" | "line+points" | "points" | "smooth";
export type Trigger = { edge?: "rising" | "falling"; source: string; level: number; position?: number; single?: boolean };
export type ScopeSettings = { mode?: ScopeMode; window?: number; ylog?: boolean; hidden?: string[]; draw?: DrawStyle; trigger?: Trigger };
/** An output a tap can wire in: an id the caller understands, and what to show. */
export type TapPort = { id: string; label: string; unit?: string };

type Props = {
  name: string;
  channels: readonly Channel[];
  /** Ticks when the channels' samples change (the trace store's version). */
  version: number;
  /** A running source: follow it. False shows a finished result (a Studio check, a stopped run). */
  live: boolean;
  settings: ScopeSettings;
  onSettings?: (next: ScopeSettings) => void;
  /** Normal size: corner ranges, a one-line legend, only ⋯ in the caption. */
  small?: boolean;
  /** What waits to be plotted before the first sample: its inputs' names. */
  placeholder?: string;
  taps?: readonly TapPort[];
  onTap?: (id: string) => void;
};

const MODES: ScopeMode[] = ["time", "xy", "fft"];
const DRAWS: DrawStyle[] = ["line", "line+points", "points", "smooth"];
/** Channel k: colour k mod 8; solid for 1–4, dashed 5–8, dotted from 9 (#37). */
const dashOf = (k: number) => (k < 4 ? [] : k < 8 ? [6, 4] : [1.5, 3]);
const colourOf = (k: number, css: CSSStyleDeclaration) => css.getPropertyValue(`--trace-${(k % 8) + 1}`).trim() || "#ff6a30";
const fmt = (x: number | null | undefined, digits = 3) => (x === null || x === undefined || !Number.isFinite(x) ? "–" : Math.abs(x) >= 1e4 || (x !== 0 && Math.abs(x) < 1e-3) ? x.toExponential(2) : x.toFixed(digits));
const download = (name: string, href: string) => {
  const a = document.createElement("a");
  a.href = href;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
};

type View = { x: [number, number] | null; y: [number, number] | null };

export function Scope({ name, channels, version, live, settings, onSettings, small = false, placeholder, taps, onTap }: Props) {
  const mode = settings.mode ?? "time";
  const draw = settings.draw ?? "line";
  const hidden = useMemo(() => new Set(settings.hidden ?? []), [settings.hidden]);
  const set = (patch: Partial<ScopeSettings>) => onSettings?.({ ...settings, ...patch });
  // Every channel keeps its slot (colour, line) when others hide: k is its place in `channels`.
  const shown = channels.map((c, k) => ({ c, k })).filter(({ c }) => !hidden.has(c.id));
  const units = [...new Set(shown.map(({ c }) => c.unit ?? ""))];
  const scaleOf = (unit: string | undefined) => (units.indexOf(unit ?? "") === 1 ? "y2" : "y");

  const [frozen, setFrozen] = useState(false);
  const [cursors, setCursors] = useState<[number, number] | null>(null);
  const [menu, setMenu] = useState<"" | "more" | "trigger" | "tap">("");
  const [tapQuery, setTapQuery] = useState("");
  const [triggered, setTriggered] = useState<number | null>(null);
  const [theme, setTheme] = useState(0);
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const view = useRef<View>({ x: null, y: null });
  const liveWindow = useRef<number | undefined>(settings.window);
  /** Each channel's highest FFT peak (Hz), as the last fft data had it: the legend reads it. */
  const peaksNow = useRef(new Map<string, number>());
  const draws = useRef<{ cursors: [number, number] | null; triggered: number | null; trigger?: Trigger; mode: ScopeMode }>({ cursors, triggered, trigger: settings.trigger, mode });
  draws.current = { cursors, triggered, trigger: settings.trigger, mode };
  const isFrozen = frozen || !live;
  const status = !live ? (channels.length ? "result" : "idle") : frozen ? "frozen" : settings.trigger && mode === "time" ? (triggered === null ? "armed" : "triggered") : "live";

  useEffect(() => {
    liveWindow.current = settings.window;
  }, [settings.window]);

  // The theme can change under a mounted Scope: the colours are read again.
  useEffect(() => {
    const mo = new MutationObserver(() => setTheme((n) => n + 1));
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme", "class"] });
    const mq = window.matchMedia?.("(prefers-color-scheme: dark)");
    const on = () => setTheme((n) => n + 1);
    mq?.addEventListener("change", on);
    return () => {
      mo.disconnect();
      mq?.removeEventListener("change", on);
    };
  }, []);

  // ---- Data for the current mode ----
  const bounds = () => {
    let t0 = Infinity, t1 = -Infinity;
    for (const { c } of shown) {
      if (!c.buf.n) continue;
      t0 = Math.min(t0, c.buf.t[0]);
      t1 = Math.max(t1, c.buf.t[c.buf.n - 1]);
    }
    return Number.isFinite(t0) ? ([t0, t1] as [number, number]) : null;
  };
  const xRange = (): [number, number] | null => {
    const b = bounds();
    if (!b) return null;
    if (isFrozen && view.current.x) return view.current.x;
    const W = liveWindow.current;
    const trig = settings.trigger;
    if (trig && mode === "time") {
      const src = channels.find((c) => c.id === trig.source);
      const width = W ?? Math.max((b[1] - b[0]) / 4, 1e-6);
      const pos = (trig.position ?? 10) / 100;
      if (src) {
        const T = findTrigger(src.buf.times, src.buf.values, trig.level, trig.edge ?? "rising", b[1] - (1 - pos) * width);
        if (T !== null) {
          if (T !== draws.current.triggered) queueMicrotask(() => setTriggered(T));
          if (trig.single && live) queueMicrotask(() => setFrozen(true));
          return [T - pos * width, T + (1 - pos) * width];
        }
      }
      return view.current.x ?? [b[1] - width, b[1]];
    }
    return W ? [b[1] - W, b[1]] : b;
  };
  const data = (): uPlot.AlignedData => {
    if (mode === "xy") {
      const [first, ...rest] = shown;
      const x = first ? Array.from(first.c.buf.values) : [];
      return [null, ...rest.map(({ c }) => [x.slice(0, c.buf.n), Array.from(c.buf.values).slice(0, x.length)])] as unknown as uPlot.AlignedData;
    }
    if (mode === "fft") {
      const [a, b] = xRange() ?? [-Infinity, Infinity];
      const tables = shown.map(({ c }) => {
        const t = c.buf.times, v = c.buf.values;
        let i0 = 0, i1 = c.buf.n;
        while (i0 < i1 && t[i0] < a) i0++;
        while (i1 > i0 && t[i1 - 1] > b) i1--;
        const dt = i1 - i0 > 1 ? (t[i1 - 1] - t[i0]) / (i1 - i0 - 1) : 0;
        const s = spectrum(v.subarray(i0, i1), dt);
        let best = -1;
        s.db.forEach((y, j) => (best < 0 || y > s.db[best]) && (best = j));
        if (best >= 0) peaksNow.current.set(c.id, s.f[best]);
        return [s.f, s.db] as uPlot.AlignedData;
      });
      return tables.length ? uPlot.join(tables) : [[]];
    }
    const tables = shown.map(({ c }) => [c.buf.times, c.buf.values] as unknown as uPlot.AlignedData);
    if (tables.length === 1) return [Array.from(shown[0].c.buf.times), Array.from(shown[0].c.buf.values)];
    return tables.length ? uPlot.join(tables) : [[]];
  };

  // ---- The plot: made again when what it draws changes shape, fed on every version ----
  const shape = JSON.stringify([mode, draw, settings.ylog ?? false, units, shown.map(({ c, k }) => [c.id, k, c.label]), theme, small]);
  useLayoutEffect(() => {
    const el = host.current;
    if (!el || !shown.length) return;
    const css = getComputedStyle(el);
    const ink = css.getPropertyValue("--text-on-screen").trim() || "#f2f2ee";
    const dim = css.getPropertyValue("--text-on-screen-muted").trim() || "#5d5d58";
    const font = `11px ${css.getPropertyValue("--font-mono").trim() || "monospace"}`;
    const grid = { stroke: "rgba(255,255,255,0.07)", width: 1, dash: [1, 3] };
    // Units are written in the screen's corners (see the JSX), not as axis titles.
    const axis = (scale: string, side: 1 | 2 | 3): uPlot.Axis => ({
      scale, side, stroke: dim, font, grid: side === 1 ? { show: false } : grid, ticks: { show: false }, size: side === 2 ? 22 : small ? 36 : 44, gap: 4,
    });
    const pathOf = (): uPlot.Series["paths"] =>
      draw === "points" ? () => null : draw === "smooth" ? uPlot.paths.spline!() : uPlot.paths.linear!();
    // xy: the trail, in order, as a path through the facet's points.
    const trail: uPlot.Series.PathBuilder = (u, sidx) => {
      const [xs, ys] = u.data[sidx] as unknown as [number[], number[]];
      const p = new Path2D();
      const scale = u.series[sidx].scale ?? "y";
      xs.forEach((x, i) => {
        const px = u.valToPos(x, "x", true), py = u.valToPos(ys[i], scale, true);
        if (i) p.lineTo(px, py);
        else p.moveTo(px, py);
      });
      return { stroke: p, fill: null, clip: null, band: null, gaps: null, flags: 0 } as unknown as uPlot.Series.Paths;
    };
    const series: uPlot.Series[] = [{}];
    const list = mode === "xy" ? shown.slice(1) : shown;
    for (const { c, k } of list) {
      const stroke = colourOf(k, css);
      series.push({
        label: c.label,
        stroke,
        width: 1.5,
        dash: dashOf(k),
        scale: mode === "fft" ? "y" : scaleOf(c.unit),
        spanGaps: true,
        paths: mode === "xy" ? trail : pathOf(),
        points: { show: draw === "points" || draw === "line+points", size: draw === "points" ? 4 : 5, width: 1, stroke, fill: draw === "points" ? stroke : "#050505" },
      });
    }
    const yRange: uPlot.Scale.Range = (_u, min, max) => {
      if (view.current.y) return view.current.y;
      if (min === null || max === null || !Number.isFinite(min) || !Number.isFinite(max)) return [-1, 1];
      if (min === max) return [min - 1, max + 1];
      const pad = (max - min) * 0.08;
      return [min - pad, max + pad];
    };
    const axes: uPlot.Axis[] = [
      { ...axis("x", 2), grid },
      axis("y", 3),
    ];
    if (mode === "time" && units.length > 1) axes.push(axis("y2", 1));
    const opts: uPlot.Options = {
      width: el.clientWidth || 300,
      height: el.clientHeight || 120,
      mode: mode === "xy" ? 2 : 1,
      pxAlign: 0,
      legend: { show: false },
      padding: [small ? 6 : 14, 18, 0, 0],
      scales: {
        x: mode === "fft" ? { time: false, distr: 3 } : { time: false, ...(mode === "time" ? { range: (_u: uPlot, min: number, max: number) => view.current.x ?? [min, max] } : {}) },
        y: mode === "fft" ? { range: yRange } : { range: yRange, ...(settings.ylog ? { distr: 3 } : {}) },
        y2: { range: (_u: uPlot, min: number, max: number) => yRange(_u, min, max, "y2") },
      },
      axes,
      series,
      select: { show: true, left: 0, top: 0, width: 0, height: 0 },
      cursor: {
        x: false,
        y: false,
        points: { show: false },
        drag: { x: true, y: true, setScale: false },
        bind: {
          // A plain drag pans (pointer handlers below); Ctrl-drag draws uPlot's zoom box.
          mousedown: (_u, _t, handler) => (e: MouseEvent) => {
            if (e.ctrlKey || e.metaKey) handler(e);
            return null;
          },
          dblclick: () => () => {
            autoscaleNow.current();
            return null;
          },
        },
      },
      hooks: {
        setSelect: [
          (u) => {
            const s = u.select;
            if (s.width > 4 && s.height > 4) {
              view.current.x = [u.posToVal(s.left, "x"), u.posToVal(s.left + s.width, "x")];
              view.current.y = [u.posToVal(s.top + s.height, "y"), u.posToVal(s.top, "y")];
              setFrozen(true);
              u.redraw(true);
            }
            u.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
          },
        ],
        draw: [(u) => decorateNow.current(u, ink)],
      },
    };
    const u = new uPlot(opts, data(), el);
    plot.current = u;
    const ro = new ResizeObserver(() => u.setSize({ width: el.clientWidth, height: el.clientHeight }));
    ro.observe(el);
    return () => {
      ro.disconnect();
      u.destroy();
      plot.current = null;
    };
  }, [shape]); // eslint-disable-line react-hooks/exhaustive-deps -- `shape` says when the plot must be made again

  // New samples: the same plot, new data, the window following the run (unless frozen).
  useEffect(() => {
    const u = plot.current;
    if (!u) return;
    if (!isFrozen) view.current.x = mode === "time" ? xRange() : null;
    u.setData(data(), true);
  }); // every render: version, settings, freeze and cursors all re-render

  // Cursors, the trigger level and its point: drawn over the traces.
  const decorate = (u: uPlot, ink: string) => {
    const { ctx } = u;
    const { left, top, width, height } = u.bbox;
    const d = draws.current;
    const r = devicePixelRatio || 1;
    ctx.save();
    ctx.font = `${10 * r}px monospace`;
    if (d.mode === "time" && d.cursors) {
      d.cursors.forEach((t, i) => {
        const x = u.valToPos(t, "x", true);
        if (x < left || x > left + width) return;
        ctx.strokeStyle = ink;
        ctx.setLineDash([3 * r, 3 * r]);
        ctx.beginPath();
        ctx.moveTo(x, top);
        ctx.lineTo(x, top + height);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.fillStyle = ink;
        ctx.fillRect(x - 6 * r, top, 12 * r, 13 * r);
        ctx.fillStyle = "#050505";
        ctx.fillText(String(i + 1), x - 3 * r, top + 10 * r);
      });
    }
    if (d.mode === "time" && d.trigger) {
      const signal = getComputedStyle(u.root).getPropertyValue("--trace-1").trim() || "#ff6a30";
      const src = channels.find((c) => c.id === d.trigger!.source);
      const y = u.valToPos(d.trigger.level, scaleOf(src?.unit), true);
      if (y >= top && y <= top + height) {
        ctx.strokeStyle = signal;
        ctx.setLineDash([4 * r, 4 * r]);
        ctx.globalAlpha = 0.6;
        ctx.beginPath();
        ctx.moveTo(left, y);
        ctx.lineTo(left + width, y);
        ctx.stroke();
        ctx.globalAlpha = 1;
        ctx.setLineDash([]);
        ctx.fillStyle = signal;
        ctx.beginPath();
        ctx.moveTo(left + width - 14 * r, y);
        ctx.lineTo(left + width - 8 * r, y - 6 * r);
        ctx.lineTo(left + width, y - 6 * r);
        ctx.lineTo(left + width, y + 6 * r);
        ctx.lineTo(left + width - 8 * r, y + 6 * r);
        ctx.closePath();
        ctx.fill();
        ctx.fillStyle = "#050505";
        ctx.fillText("T", left + width - 8 * r, y + 4 * r);
      }
      if (d.triggered !== null) {
        const x = u.valToPos(d.triggered, "x", true);
        ctx.fillStyle = signal;
        ctx.beginPath();
        ctx.moveTo(x - 5 * r, top);
        ctx.lineTo(x + 5 * r, top);
        ctx.lineTo(x, top + 7 * r);
        ctx.closePath();
        ctx.fill();
      }
    }
    if (d.mode === "xy") {
      // The newest point of each trail is a dot.
      u.series.forEach((s, i) => {
        if (!i) return;
        const [xs, ys] = u.data[i] as unknown as [number[], number[]];
        if (!xs?.length) return;
        ctx.fillStyle = String(s.stroke instanceof Function ? s.stroke(u, i) : s.stroke);
        ctx.beginPath();
        ctx.arc(u.valToPos(xs[xs.length - 1], "x", true), u.valToPos(ys[ys.length - 1], s.scale ?? "y", true), 3.5 * r, 0, 2 * Math.PI);
        ctx.fill();
      });
    }
    ctx.restore();
  };

  const decorateNow = useRef(decorate);
  decorateNow.current = decorate;

  // ---- Gestures: drag pans (or moves a cursor or the trigger level), Ctrl+wheel zooms ----
  const autoscale = () => {
    view.current = { x: null, y: null };
    liveWindow.current = settings.window;
    setFrozen(false);
    setTriggered(null);
    plot.current?.redraw(true);
  };
  const autoscaleNow = useRef(autoscale);
  autoscaleNow.current = autoscale;
  const drag = useRef<null | { kind: "pan" | "cursor" | "level"; i?: number; x0: number; min: number; max: number }>(null);
  const local = (e: { clientX: number; clientY: number }) => {
    const u = plot.current!;
    const r = u.over.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top, w: r.width, h: r.height };
  };
  const onPointerDown = (e: ReactPointerEvent) => {
    const u = plot.current;
    if (!u || e.button !== 0 || e.ctrlKey || e.metaKey || mode !== "time" || !(e.target as Element).closest(".u-over")) return;
    const p = local(e);
    const min = u.scales.x.min ?? 0, max = u.scales.x.max ?? 1;
    const near = cursors?.findIndex((t) => Math.abs(u.valToPos(t, "x") - p.x) < 7) ?? -1;
    const trig = settings.trigger;
    if (near >= 0) drag.current = { kind: "cursor", i: near, x0: p.x, min, max };
    else if (trig && p.x > p.w - 18 && Math.abs(u.valToPos(trig.level, scaleOf(channels.find((c) => c.id === trig.source)?.unit)) - p.y) < 9)
      drag.current = { kind: "level", x0: p.y, min, max };
    else drag.current = { kind: "pan", x0: p.x, min, max };
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    e.stopPropagation(); // outside Arrange the Board must not start a drag of the cell
  };
  const onPointerMove = (e: ReactPointerEvent) => {
    const u = plot.current, d = drag.current;
    if (!u || !d) return;
    const p = local(e);
    if (d.kind === "pan") {
      const shift = ((p.x - d.x0) * (d.max - d.min)) / p.w;
      view.current.x = [d.min - shift, d.max - shift];
      if (!frozen) setFrozen(true); // panning back in time freezes the Scope
      u.redraw(true);
    } else if (d.kind === "cursor" && cursors) {
      const next: [number, number] = [...cursors];
      next[d.i!] = u.posToVal(p.x, "x");
      setCursors(next);
    } else if (d.kind === "level" && settings.trigger) {
      const scale = scaleOf(channels.find((c) => c.id === settings.trigger!.source)?.unit);
      draws.current.trigger = { ...settings.trigger, level: u.posToVal(p.y, scale) };
      u.redraw(false);
    }
  };
  const onPointerUp = () => {
    const d = drag.current;
    drag.current = null;
    if (d?.kind === "level" && draws.current.trigger) set({ trigger: { ...draws.current.trigger, level: +draws.current.trigger.level.toPrecision(4) } });
  };
  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const wheel = (e: WheelEvent) => {
      const u = plot.current;
      if (!u || !(e.ctrlKey || e.metaKey) || mode === "xy") return; // a plain wheel scrolls the Board
      e.preventDefault();
      const p = local(e);
      const f = e.deltaY > 0 ? 1.25 : 0.8;
      if (e.shiftKey) {
        const at = u.posToVal(p.y, "y"), [a, b] = [u.scales.y.min ?? 0, u.scales.y.max ?? 1];
        view.current.y = [at - (at - a) * f, at + (b - at) * f];
      } else {
        const at = u.posToVal(p.x, "x"), [a, b] = [u.scales.x.min ?? 0, u.scales.x.max ?? 1];
        view.current.x = [at - (at - a) * f, at + (b - at) * f];
        if (!isFrozen) liveWindow.current = view.current.x[1] - view.current.x[0]; // zooming while live keeps following, with the new window
      }
      u.redraw(true);
    };
    el.addEventListener("wheel", wheel, { passive: false });
    return () => el.removeEventListener("wheel", wheel);
  });

  const toggleCursors = () => {
    const u = plot.current;
    if (cursors || !u) return setCursors(null);
    const a = u.scales.x.min ?? 0, b = u.scales.x.max ?? 1;
    setCursors([a + (b - a) / 3, a + (2 * (b - a)) / 3]);
  };
  const toggleFreeze = () => {
    if (frozen) autoscale();
    else {
      const u = plot.current;
      if (u && mode === "time") view.current.x = [u.scales.x.min ?? 0, u.scales.x.max ?? 1];
      setFrozen(true);
    }
  };
  const onKey = (e: ReactKeyboardEvent) => {
    if ((e.target as Element).closest("input, select, button")) return;
    const k = e.key.toLowerCase();
    if (k === "f") toggleFreeze();
    else if (k === "c") toggleCursors();
    else if (k === "t") setMenu((m) => (m === "trigger" ? "" : "trigger"));
    else if (k === "a") autoscale();
    else if (k === "escape") setMenu("");
    else return;
    e.preventDefault();
    e.stopPropagation();
  };

  // ---- Readouts ----
  const at = (c: Channel, t: number) => valueAt(c.buf.times, c.buf.values, t);
  const peaks = peaksNow.current;
  const legendValue = (c: Channel) =>
    mode === "fft" ? (peaks.has(c.id) ? `${fmt(peaks.get(c.id), 2)}` : "–") : cursors && mode === "time" ? fmt(at(c, cursors[1])) : fmt(c.buf.last);
  const legendUnit = (c: Channel) => (mode === "fft" ? "Hz" : c.unit);

  const exportCsv = (all: boolean) => {
    const u = plot.current;
    const [a, b] = all || !u || mode !== "time" ? [-Infinity, Infinity] : [u.scales.x.min ?? -Infinity, u.scales.x.max ?? Infinity];
    const text = toCsv(shown.map(({ c }) => ({ label: c.label, unit: c.unit, t: c.buf.times, v: c.buf.values })), a, b);
    download(`${name}.csv`, URL.createObjectURL(new Blob([text], { type: "text/csv" })));
    setMenu("");
  };
  const exportPng = () => {
    const u = plot.current;
    if (!u) return;
    // The screen, with the legend written along its top.
    const canvas = document.createElement("canvas");
    canvas.width = u.ctx.canvas.width;
    canvas.height = u.ctx.canvas.height;
    const g = canvas.getContext("2d")!;
    g.fillStyle = "#050505";
    g.fillRect(0, 0, canvas.width, canvas.height);
    g.drawImage(u.ctx.canvas, 0, 0);
    const css = getComputedStyle(u.root);
    g.font = `${11 * (devicePixelRatio || 1)}px monospace`;
    let x = 8;
    shown.forEach(({ c, k }) => {
      g.fillStyle = colourOf(k, css);
      const text = `${c.label} ${legendValue(c)}${legendUnit(c) ? ` ${legendUnit(c)}` : ""}`;
      g.fillText(text, x, 14 * (devicePixelRatio || 1));
      x += g.measureText(text).width + 16;
    });
    download(`${name}.png`, canvas.toDataURL("image/png"));
    setMenu("");
  };
  const copyCursors = () => {
    if (!cursors) return;
    const [t1, t2] = cursors;
    const lines = [`t1 ${fmt(t1)} s`, `t2 ${fmt(t2)} s`, `Δt ${fmt(t2 - t1)} s`, `1/Δt ${fmt(1 / (t2 - t1))} Hz`];
    for (const { c } of shown) {
      const a = at(c, t1), b = at(c, t2);
      lines.push(`Δ${c.label} ${a === null || b === null ? "–" : fmt(b - a)}${c.unit ? ` ${c.unit}` : ""}`);
    }
    void navigator.clipboard?.writeText(lines.join("\n"));
    setMenu("");
  };

  const toggle = (on: boolean, label: string) => (on ? `[ ${label} ]` : label);
  const trig = settings.trigger;
  const tapList = (taps ?? []).filter((t) => t.label.toLowerCase().includes(tapQuery.toLowerCase()));

  return (
    <figure className={`instrument scope${channels.length ? "" : " idle"}${small ? " small" : ""}`} data-version={version} tabIndex={0} onKeyDown={onKey} aria-label={`Scope ${name}`}>
      <div className={`screen${cursors && mode === "time" && channels.length ? " with-readout" : ""}`}>
        {channels.length > 0 && (
          <div className="scope-legend" role="list">
            {(mode === "xy" ? shown.slice(0, 1) : []).map(({ c }) => (
              <span key={`x-${c.id}`} className="scope-entry" role="listitem">
                x {c.label} <b>{fmt(c.buf.last)}</b>
                {c.unit && <i>{c.unit}</i>}
              </span>
            ))}
            {channels.map((c, k) => (mode === "xy" && shown[0]?.c === c ? null : (
              <button
                key={c.id}
                className={`scope-entry${hidden.has(c.id) ? " off" : ""}`}
                role="listitem"
                title={hidden.has(c.id) ? `Show ${c.label}` : `Hide ${c.label}`}
                onClick={() => set({ hidden: hidden.has(c.id) ? [...hidden].filter((h) => h !== c.id) : [...hidden, c.id] })}
              >
                <svg width="16" height="6" aria-hidden>
                  <line x1="0" y1="3" x2="16" y2="3" style={{ stroke: `var(--trace-${(k % 8) + 1})` }} strokeWidth="1.6" strokeDasharray={dashOf(k).join(" ") || undefined} />
                </svg>
                {mode === "xy" && "y "}
                {c.label} <b>{legendValue(c)}</b>
                {legendUnit(c) && <i>{legendUnit(c)}</i>}
              </button>
            )))}
          </div>
        )}
        <div
          ref={host}
          className="scope-plot"
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={onPointerUp}
        />
        {channels.length > 0 && (
          <>
            <span className="scope-unit y">{mode === "fft" ? "dB" : mode === "xy" ? shown[1]?.c.unit : units[0]}</span>
            {mode === "time" && units.length > 1 && <span className="scope-unit y2">{units[1]}</span>}
            <span className="scope-unit x">{mode === "fft" ? "Hz" : mode === "xy" ? shown[0]?.c.unit : "s"}</span>
          </>
        )}
                {!channels.length && <output className="scope-placeholder">{placeholder ?? "scope"}</output>}
        {cursors && mode === "time" && channels.length > 0 && (
          <div className="scope-readout">
            <span>1 {fmt(cursors[0])} s</span>
            <span>2 {fmt(cursors[1])} s</span>
            <span>Δt {fmt(cursors[1] - cursors[0])} s</span>
            <span>1/Δt {fmt(1 / (cursors[1] - cursors[0]))} Hz</span>
            {shown.map(({ c, k }) => {
              const a = at(c, cursors[0]), b = at(c, cursors[1]);
              return (
                <span key={c.id}>
                  <svg width="12" height="6" aria-hidden>
                    <line x1="0" y1="3" x2="12" y2="3" style={{ stroke: `var(--trace-${(k % 8) + 1})` }} strokeWidth="1.6" strokeDasharray={dashOf(k).join(" ") || undefined} />
                  </svg>{" "}
                  Δ{c.label} {a === null || b === null ? "–" : `${b - a >= 0 ? "+" : ""}${fmt(b - a)}`}
                  {c.unit ? ` ${c.unit}` : ""}
                </span>
              );
            })}
          </div>
        )}
      </div>
      <figcaption>
        <span>{name}</span>
        <span className={`scope-status ${status}`}>{status}</span>
        <span className="scope-keys">
          {!small && (
            <>
              <span className="scope-modes" role="radiogroup" aria-label="Mode">
                {MODES.map((m) => (
                  <button key={m} role="radio" aria-checked={mode === m} className={mode === m ? "on" : ""} onClick={() => set({ mode: m === "time" ? undefined : m })}>
                    {mode === m ? `[ ${m} ]` : m}
                  </button>
                ))}
              </span>
              {onTap && (
                <button className="key small" title="Wire an output into this Scope" onClick={() => setMenu(menu === "tap" ? "" : "tap")}>
                  + tap
                </button>
              )}
              <button className={`key small${isFrozen && live ? " on" : ""}`} disabled={!live} title="Freeze or follow (F)" onClick={toggleFreeze}>
                {toggle(frozen && live, "freeze")}
              </button>
              <button className={`key small${cursors ? " on" : ""}`} disabled={mode !== "time" || !channels.length} title="Two cursors and their deltas (C)" onClick={toggleCursors}>
                {toggle(!!cursors, "cursors")}
              </button>
              <button className={`key small${trig ? " on" : ""}`} disabled={mode !== "time"} title="Trigger (T)" onClick={() => setMenu(menu === "trigger" ? "" : "trigger")}>
                {toggle(!!trig, "trigger")}
              </button>
            </>
          )}
          <button className="key small" title="More" aria-haspopup="menu" aria-expanded={menu === "more"} onClick={() => setMenu(menu === "more" ? "" : "more")}>
            ⋯
          </button>
        </span>
        {menu === "more" && (
          <div className="scope-pop" role="menu">
            {small && (
              <div className="scope-row">
                {MODES.map((m) => (
                  <button key={m} className={`opt${mode === m ? " on" : ""}`} onClick={() => set({ mode: m === "time" ? undefined : m })}>
                    {mode === m ? `[ ${m} ]` : m}
                  </button>
                ))}
              </div>
            )}
            <div className="scope-row" aria-label="Draw">
              {DRAWS.map((d) => (
                <button key={d} className={`opt${draw === d ? " on" : ""}`} onClick={() => set({ draw: d === "line" ? undefined : d })}>
                  {draw === d ? `[ ${d} ]` : d}
                </button>
              ))}
            </div>
            <label className="scope-row">
              window
              <input
                className="well"
                type="number"
                min={0}
                step="any"
                placeholder="whole run"
                defaultValue={settings.window ?? ""}
                onBlur={(e) => set({ window: Number(e.target.value) > 0 ? Number(e.target.value) : undefined })}
              />
              s
            </label>
            <label className="scope-row">
              <input type="checkbox" checked={!!settings.ylog} disabled={mode !== "time"} onChange={(e) => set({ ylog: e.target.checked || undefined })} /> log y
            </label>
            {small && (
              <>
                <button className="opt" disabled={!live} onClick={toggleFreeze}>{toggle(frozen && live, "freeze")}</button>
                <button className="opt" disabled={mode !== "time"} onClick={toggleCursors}>{toggle(!!cursors, "cursors")}</button>
                <button className="opt" disabled={mode !== "time"} onClick={() => setMenu("trigger")}>{toggle(!!trig, "trigger")}</button>
              </>
            )}
            <hr />
            <button className="opt" onClick={() => exportCsv(false)} disabled={!channels.length}>CSV, visible range</button>
            <button className="opt" onClick={() => exportCsv(true)} disabled={!channels.length}>CSV, all</button>
            <button className="opt" onClick={exportPng} disabled={!channels.length}>PNG</button>
            <button className="opt" onClick={copyCursors} disabled={!cursors}>Copy cursor values</button>
          </div>
        )}
        {menu === "trigger" && (
          <div className="scope-pop" role="dialog" aria-label="Trigger">
            <div className="scope-row">
              {(["rising", "falling"] as const).map((edge) => (
                <button key={edge} className={`opt${(trig?.edge ?? "rising") === edge ? " on" : ""}`} onClick={() => set({ trigger: { source: trig?.source ?? shown[0]?.c.id ?? "", level: trig?.level ?? 0, ...trig, edge } })}>
                  {(trig?.edge ?? "rising") === edge ? `[ ${edge} ]` : edge}
                </button>
              ))}
            </div>
            <label className="scope-row">
              source
              <select className="well" value={trig?.source ?? ""} onChange={(e) => set({ trigger: { level: 0, ...trig, source: e.target.value } })}>
                {!trig && <option value="">pick a channel</option>}
                {channels.map((c) => (
                  <option key={c.id} value={c.id}>{c.label}</option>
                ))}
              </select>
            </label>
            <label className="scope-row">
              level
              <input className="well" type="number" step="any" defaultValue={trig?.level ?? 0} key={trig?.level} onBlur={(e) => trig && set({ trigger: { ...trig, level: Number(e.target.value) || 0 } })} />
            </label>
            <label className="scope-row">
              position
              <input className="well" type="number" min={0} max={100} defaultValue={trig?.position ?? 10} onBlur={(e) => trig && set({ trigger: { ...trig, position: Math.min(100, Math.max(0, Number(e.target.value) || 0)) } })} />%
            </label>
            <div className="scope-row">
              {(["auto", "single"] as const).map((m) => (
                <button key={m} className={`opt${(trig?.single ? "single" : "auto") === m ? " on" : ""}`} disabled={!trig} onClick={() => { if (trig) { set({ trigger: { ...trig, single: m === "single" || undefined } }); autoscale(); } }}>
                  {(trig?.single ? "single" : "auto") === m ? `[ ${m} ]` : m}
                </button>
              ))}
            </div>
            <button className="opt" disabled={!trig} onClick={() => { set({ trigger: undefined }); setMenu(""); setTriggered(null); }}>
              No trigger
            </button>
          </div>
        )}
        {menu === "tap" && (
          <div className="scope-pop" role="dialog" aria-label="Tap an output">
            <input className="well" autoFocus placeholder="/ search outputs" value={tapQuery} onChange={(e) => setTapQuery(e.target.value)} />
            <div className="scope-taps">
              {tapList.length === 0 && <span className="muted">no outputs</span>}
              {tapList.slice(0, 40).map((t) => (
                <button key={t.id} className="opt" onClick={() => { onTap?.(t.id); setMenu(""); setTapQuery(""); }}>
                  {t.label}
                  {t.unit && <i> {t.unit}</i>}
                </button>
              ))}
            </div>
          </div>
        )}
      </figcaption>
    </figure>
  );
}
