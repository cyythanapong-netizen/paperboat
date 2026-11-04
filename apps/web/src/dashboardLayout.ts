import type { DashboardLayout } from "./protocol";

export type Size = "compact" | "normal" | "large" | "xlarge";

/** What an instrument occupies, in grid cells (columns x rows). */
export const CELLS: Record<Size, [number, number]> = { compact: [1, 1], normal: [2, 1], large: [2, 2], xlarge: [3, 3] };
export const SIZES: Size[] = ["compact", "normal", "large", "xlarge"];
/** What the size picker shows. */
export const SIZE_LABEL: Record<Size, string> = { compact: "S", normal: "M", large: "L", xlarge: "XL" };

/** Columns of the grid when a layout does not say. */
export const DEFAULT_COLUMNS = 6;

/** A readout is a card, a plot is read at a glance, a picture is studied. */
export function defaultSize(kind: string): Size {
  return kind === "display" ? "compact" : kind === "image" ? "large" : "normal";
}

/** A monitor on the Flow canvas: what the grid needs to place its instrument. */
export type Monitor = { kind: string; x: number; y: number };

type Deco = NonNullable<DashboardLayout["decorations"]>[number];
export type DecoKind = Deco["kind"];

/** An instrument on the grid. `tab` is its tab's name, "" when the layout has no tabs. */
export type Placed = { block: string; size: Size; col: number; row: number; w: number; h: number; tab: string };

/**
 * A decoration on the grid (a section, a separator, a label), by its place in the layout's
 * list. A section or separator takes no cells: it is drawn in the gaps. A label takes cells
 * like an instrument. `hidden`: not drawn on a screen narrower than the layout, where cells
 * mean little (labels flow in reading order instead).
 */
export type PlacedDeco = { index: number; kind: DecoKind; text: string; style: "heading" | "note"; col: number; row: number; w: number; h: number; tab: string; hidden: boolean };

/** A layout laid out on a grid of `columns`: what is drawn, and what an edit starts from. */
export type Arranged = { columns: number; narrowed: boolean; tabs: string[]; instruments: Placed[]; decorations: PlacedDeco[] };

const KIND_RANK: Record<string, number> = { display: 0, image: 1 };

/**
 * The automatic order, which is where an instrument with no saved place goes: readouts,
 * then pictures, then plots; within a kind the Flow canvas, left to right, then top to
 * bottom (a pipeline's stages read in order, and moving a block moves its instrument).
 */
export function autoOrder(monitors: Record<string, Monitor>): string[] {
  const rank = (n: string) => KIND_RANK[monitors[n].kind] ?? 2;
  return Object.keys(monitors).sort((a, b) => rank(a) - rank(b) || monitors[a].x - monitors[b].x || monitors[a].y - monitors[b].y);
}

/** A label and the console take cells like an instrument; a section and a separator are drawn in the gaps. */
const takesCells = (kind: DecoKind) => kind === "label" || kind === "console";

/** A decoration's size in cells: a separator is one cell wide or tall and runs `span` cells. */
function decoCells(d: Deco, columns: number): [number, number] {
  const n = typeof d.span === "number" ? d.span : 1;
  const [w, h] = Array.isArray(d.span) ? d.span : d.kind === "section" ? [2, 2] : d.kind === "console" ? [4, 3] : [2, 1];
  const cells: [number, number] = d.kind === "hline" ? [n, 1] : d.kind === "vline" ? [1, n] : [w, h];
  return [Math.min(Math.max(1, cells[0]), columns), Math.max(1, cells[1])];
}

/**
 * Lay the layout out: every monitor's instrument, on the tab it names (else the first), on a
 * grid of `columns`. Per tab, labels sit where they say and take their cells; saved places
 * of instruments (`layout`) are kept, in the order the layout lists them: one that no longer
 * fits (taken, or past the edge) moves down to the first free cell below. The rest, and a
 * layout's blocks that are no longer monitors (ignored), fall in by `autoOrder`, each in the
 * first free spot. Sections and separators take no cells. On a screen narrower than the
 * layout (`columns` fewer than it was made for) saved cells mean little: labels and
 * instruments keep their reading order (saved row, then column) and flow into the fewer
 * columns, and sections and separators are hidden.
 */
export function arrange(monitors: Record<string, Monitor>, layout: DashboardLayout | undefined, columns: number): Arranged {
  const tabs = [...new Set(layout?.tabs ?? [])];
  const tabOf = (t: string | undefined) => (tabs.length === 0 ? "" : t !== undefined && tabs.includes(t) ? t : tabs[0]);
  const narrowed = columns < (layout?.columns ?? DEFAULT_COLUMNS);
  const saved = new Map((layout?.instruments ?? []).filter((i) => monitors[i.block]).map((i) => [i.block, i]));
  const decorations: PlacedDeco[] = (layout?.decorations ?? []).map((d, index) => {
    const [w, h] = decoCells(d, columns);
    return {
      index,
      kind: d.kind,
      text: d.text ?? "",
      style: d.style ?? "heading",
      col: Math.min(d.at[0], columns - w),
      row: d.at[1],
      w,
      h,
      tab: tabOf(d.tab),
      hidden: narrowed && !takesCells(d.kind),
    };
  });
  const instruments: Placed[] = [];
  const order = autoOrder(monitors);

  for (const tab of tabs.length ? tabs : [""]) {
    const taken = new Set<string>();
    const free = (c: number, r: number, w: number, h: number) => {
      for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) if (taken.has(`${c + i},${r + j}`)) return false;
      return true;
    };
    const claim = (c: number, r: number, w: number, h: number) => {
      for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) taken.add(`${c + i},${r + j}`);
    };
    // The first free spot for w x h from (c, r): wanted cell first, then the rest of the row, then the rows below.
    const spot = (c: number, r: number, w: number, h: number): [number, number] => {
      for (let row = r; ; row++) {
        for (let col = row === r ? Math.min(c, columns - w) : 0; col + w <= columns; col++) if (free(col, row, w, h)) return [col, row];
      }
    };
    const labels = decorations.filter((d) => d.tab === tab && takesCells(d.kind));
    const mine = [...saved.values()].filter((i) => tabOf(i.tab) === tab);
    const sized = (block: string) => {
      const size = saved.get(block)?.size ?? defaultSize(monitors[block].kind);
      return { size, w: Math.min(CELLS[size][0], columns), h: CELLS[size][1] };
    };
    const putInstrument = (block: string, c: number, r: number) => {
      const { size, w, h } = sized(block);
      const [col, row] = spot(c, r, w, h);
      claim(col, row, w, h);
      instruments.push({ block, size, col, row, w, h, tab });
    };
    if (!narrowed) {
      for (const l of labels) claim(l.col, l.row, l.w, l.h); // a label is where it says, and takes its cells
      for (const i of mine) if (i.at) putInstrument(i.block, i.at[0], i.at[1]);
    } else {
      // Reading order: saved row, then column, labels and instruments together.
      type Item = { key: [number, number]; run: () => void };
      const items: Item[] = [
        ...labels.map((l): Item => ({
          key: [l.row, l.col],
          run: () => {
            const [col, row] = spot(0, 0, l.w, l.h);
            claim(col, row, l.w, l.h);
            l.col = col;
            l.row = row;
          },
        })),
        ...mine.filter((i) => i.at).map((i): Item => ({ key: [i.at![1], i.at![0]], run: () => putInstrument(i.block, 0, 0) })),
      ].sort((a, b) => a.key[0] - b.key[0] || a.key[1] - b.key[1]);
      for (const item of items) item.run();
    }
    // The instruments with no place on this tab: not listed, or listed without `at`; unlisted ones are on the first tab.
    const done = new Set(instruments.filter((p) => p.tab === tab).map((p) => p.block));
    for (const block of order) {
      if (done.has(block)) continue;
      const listed = saved.get(block);
      if (listed ? tabOf(listed.tab) !== tab : tab !== (tabs[0] ?? "")) continue;
      putInstrument(block, 0, 0);
    }
  }
  return { columns, narrowed, tabs, instruments, decorations };
}

/** The rows a tab's grid needs, counting what is on it. */
export function rowsOf(a: Arranged, tab: string): number {
  return Math.max(
    0,
    ...a.instruments.filter((p) => p.tab === tab).map((p) => p.row + p.h),
    ...a.decorations.filter((d) => d.tab === tab && !d.hidden).map((d) => d.row + d.h),
  );
}

// ---- Saving: the layout that records an arrangement exactly, and the edits that make one ----

/** The layout that records `a`; `first`, an instrument, is listed first so what it lands on makes way. */
export function layoutOf(a: Arranged, first?: string): DashboardLayout {
  const tabbed = a.tabs.length > 0;
  const instruments = a.instruments.map((p) => ({
    block: p.block,
    size: p.size,
    ...(p.col >= 0 ? { at: [p.col, p.row] as [number, number] } : {}),
    ...(tabbed ? { tab: p.tab } : {}),
  }));
  const lead = instruments.findIndex((i) => i.block === first);
  if (lead > 0) instruments.unshift(...instruments.splice(lead, 1));
  const decorations: Deco[] = a.decorations.map((d) => {
    const line = d.kind === "hline" || d.kind === "vline";
    return {
      kind: d.kind,
      ...(d.kind === "label" || (d.kind === "section" && d.text) ? { text: d.text } : {}),
      at: [d.col, d.row] as [number, number],
      span: line ? (d.kind === "hline" ? d.w : d.h) : ([d.w, d.h] as [number, number]),
      ...(d.kind === "label" && d.style === "note" ? { style: "note" as const } : {}),
      ...(tabbed ? { tab: d.tab } : {}),
    };
  });
  return {
    columns: a.columns,
    ...(tabbed ? { tabs: a.tabs } : {}),
    instruments,
    ...(decorations.length ? { decorations } : {}),
  };
}

/** Copy `a`, change the copy, and give the layout that records it. */
function edit(a: Arranged, change: (m: Arranged) => string | void): DashboardLayout {
  const m = structuredClone(a);
  return layoutOf(m, change(m) || undefined);
}

export const moveInstrument = (a: Arranged, block: string, col: number, row: number) =>
  edit(a, (m) => {
    const p = m.instruments.find((q) => q.block === block)!;
    p.col = Math.max(0, col);
    p.row = Math.max(0, row);
    return block;
  });

export const resizeInstrument = (a: Arranged, block: string, size: Size) =>
  edit(a, (m) => {
    m.instruments.find((q) => q.block === block)!.size = size;
    return block;
  });

/** An instrument to another tab: it takes the first free cell there. */
export const moveToTab = (a: Arranged, block: string, tab: string) =>
  edit(a, (m) => {
    const p = m.instruments.find((q) => q.block === block)!;
    p.tab = tab;
    p.col = -1;
  });

export const addDecoration = (a: Arranged, kind: DecoKind, tab: string) =>
  edit(a, (m) => {
    const bottom = rowsOf(m, tab);
    const [w, h] = kind === "section" ? [3, 2] : kind === "console" ? [4, 3] : kind === "label" ? [2, 1] : kind === "hline" ? [m.columns, 1] : [1, Math.max(2, bottom)];
    m.decorations.push({
      index: m.decorations.length,
      kind,
      text: kind === "label" ? "Label" : kind === "section" ? "Section" : "",
      style: "heading",
      col: kind === "vline" ? Math.min(1, m.columns - 1) : 0,
      row: kind === "vline" ? 0 : bottom,
      w: Math.min(w, m.columns),
      h,
      tab,
      hidden: false,
    });
  });

export const moveDecoration = (a: Arranged, index: number, col: number, row: number) =>
  edit(a, (m) => {
    const d = m.decorations.find((x) => x.index === index)!;
    d.col = Math.min(Math.max(0, col), m.columns - d.w);
    d.row = Math.max(0, row);
  });

/** A section or label to w x h cells, or a separator to a length of w (hline) or h (vline). */
export const resizeDecoration = (a: Arranged, index: number, w: number, h: number) =>
  edit(a, (m) => {
    const d = m.decorations.find((x) => x.index === index)!;
    if (d.kind === "hline") d.w = Math.min(Math.max(1, w), m.columns - d.col);
    else if (d.kind === "vline") d.h = Math.max(1, h);
    else {
      d.w = Math.min(Math.max(1, w), m.columns - d.col);
      d.h = Math.max(1, h);
    }
  });

export const editDecoration = (a: Arranged, index: number, patch: Partial<Pick<PlacedDeco, "text" | "style">>) =>
  edit(a, (m) => void Object.assign(m.decorations.find((x) => x.index === index)!, patch));

export const removeDecoration = (a: Arranged, index: number) =>
  edit(a, (m) => {
    m.decorations = m.decorations.filter((x) => x.index !== index).map((d, i) => ({ ...d, index: i }));
  });

/** A new tab. The first one made turns the page into two: what is there stays on "Main". */
export const addTab = (a: Arranged, name: string) =>
  edit(a, (m) => {
    if (m.tabs.length === 0) {
      m.tabs = ["Main"];
      for (const p of m.instruments) p.tab = "Main";
      for (const d of m.decorations) d.tab = "Main";
    }
    m.tabs.push(name);
  });

export const renameTab = (a: Arranged, from: string, to: string) =>
  edit(a, (m) => {
    if (!to || (to !== from && m.tabs.includes(to))) return;
    m.tabs = m.tabs.map((t) => (t === from ? to : t));
    for (const p of m.instruments) if (p.tab === from) p.tab = to;
    for (const d of m.decorations) if (d.tab === from) d.tab = to;
  });

export const tabIsEmpty = (a: Arranged, tab: string) => !a.instruments.some((p) => p.tab === tab) && !a.decorations.some((d) => d.tab === tab);

/** An empty tab removed; with one tab left the strip goes and the page is one again. */
export const removeTab = (a: Arranged, tab: string) =>
  edit(a, (m) => {
    if (!tabIsEmpty(m, tab)) return;
    m.tabs = m.tabs.filter((t) => t !== tab);
    if (m.tabs.length === 1) {
      m.tabs = [];
      for (const p of m.instruments) p.tab = "";
      for (const d of m.decorations) d.tab = "";
    }
  });

/** A name for a new tab that no tab has. */
export function freshTabName(tabs: string[]): string {
  let n = tabs.length + 1;
  while (tabs.includes(`Tab ${n}`)) n++;
  return `Tab ${n}`;
}

// ---- Text with values in it: a formatted string ----

/** A piece of a decoration's text: plain, or the live value of a monitor. */
export type TextPart = string | { name: string; value: string; variable: boolean; shadowed: boolean };

/** A value as the display instrument shows it; `spec` is `.Nf` (N decimals) as in `{speed:.2f}`. */
function showValue(v: unknown, spec: string): string {
  if (v === undefined || v === null) return "–";
  const fixed = /^\.(\d{1,2})f$/.exec(spec);
  if (typeof v === "number") return fixed ? v.toFixed(+fixed[1]) : String(+v.toFixed(4));
  if (typeof v === "boolean") return v ? "on" : "off";
  return String(v);
}

/**
 * Split `text` the way a formatted string reads: `{block}` is the latest value of that
 * monitor block ("–" before a run), `{block:.2f}` with that many decimals, `{{` and `}}`
 * are braces. A block name may hold a colon: the whole name is tried before the spec.
 */
export function formatText(text: string, latest: Record<string, unknown>, vars: Record<string, unknown> = {}): TextPart[] {
  const parts: TextPart[] = [];
  const known = (n: string) => n in latest || n in vars;
  const re = /\{\{|\}\}|\{([^{}]+)\}/g;
  let plain = "";
  let at = 0;
  for (let m = re.exec(text); m; m = re.exec(text)) {
    plain += text.slice(at, m.index);
    at = m.index + m[0].length;
    if (m[1] === undefined) {
      plain += m[0][0];
      continue;
    }
    const cut = m[1].lastIndexOf(":");
    const [name, spec] = known(m[1]) || cut < 0 ? [m[1], ""] : [m[1].slice(0, cut), m[1].slice(cut + 1)];
    if (plain) parts.push(plain);
    plain = "";
    const variable = !(name in latest) && name in vars;
    parts.push({ name, value: showValue(variable ? vars[name] : latest[name], spec), variable, shadowed: name in latest && name in vars });
  }
  plain += text.slice(at);
  if (plain) parts.push(plain);
  return parts;
}

/** `text` with the reference `{from}` (or `{from:spec}`) renamed; `{{from}}` is plain text and stays. */
export function renameInText(text: string, from: string, to: string): string {
  return text.replace(/\{\{|\}\}|\{([^{}]+)\}/g, (m, body?: string) => {
    if (body === undefined) return m;
    if (body === from) return `{${to}}`;
    return body.startsWith(`${from}:`) ? `{${to}${body.slice(from.length)}}` : m;
  });
}

/** `text` with the reference `{name}` replaced by `value` as a chip shows it (the variable is going away). */
export function inlineInText(text: string, name: string, value: unknown): string {
  return text.replace(/\{\{|\}\}|\{([^{}]+)\}/g, (m, body?: string) => {
    if (body === undefined) return m;
    const spec = body === name ? "" : body.startsWith(`${name}:`) ? body.slice(name.length + 1) : null;
    return spec === null ? m : showValue(value, spec).replace(/[{}]/g, (c) => c + c);
  });
}

/** Labels and section titles in `layout` that refer to `{name}`, by their place in the layout's list. */
export function textUsing(layout: DashboardLayout | undefined, name: string): { index: number; kind: DecoKind; text: string }[] {
  return (layout?.decorations ?? []).flatMap((d, index) => (d.text !== undefined && renameInText(d.text, name, `${name}_`) !== d.text ? [{ index, kind: d.kind, text: d.text }] : []));
}

/** `layout` with every decoration's text mapped by `f`; the same object when nothing changes. */
export function mapTexts(layout: DashboardLayout, f: (text: string) => string): DashboardLayout {
  let changed = false;
  const decorations = layout.decorations?.map((d) => {
    const text = d.text === undefined ? undefined : f(d.text);
    if (text === d.text) return d;
    changed = true;
    return { ...d, text };
  });
  return changed ? { ...layout, decorations } : layout;
}

/** A saved layout, if the browser has one for this backend. */
export function loadSaved(key: string): DashboardLayout | undefined {
  try {
    const v = JSON.parse(localStorage.getItem(`paperboat.dashboard:${key}`) ?? "null");
    return v && Array.isArray(v.instruments) ? (v as DashboardLayout) : undefined;
  } catch {
    return undefined;
  }
}

/** Keep the user's layout for this backend in the browser, as the working copy: a project file (#82) saves and restores it. `undefined` forgets it. */
export function save(key: string, layout: DashboardLayout | undefined) {
  try {
    if (layout) localStorage.setItem(`paperboat.dashboard:${key}`, JSON.stringify(layout));
    else localStorage.removeItem(`paperboat.dashboard:${key}`);
  } catch {
    /* storage blocked: the layout lasts for the session only */
  }
}
