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

export type Placed = { block: string; size: Size; col: number; row: number; w: number; h: number };

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

/**
 * Put every monitor's instrument on a grid of `columns` columns. Saved places (`layout`)
 * are kept, in the order the layout lists them: one that no longer fits (taken by an
 * earlier one, or past the edge) moves down to the first free cell below. The rest, and
 * a layout's blocks that are no longer monitors (ignored), fall in by `autoOrder`, each
 * in the first free spot. On a screen narrower than the layout (`columns` fewer than it
 * was made for) saved cells mean little: the instruments keep their reading order
 * (saved row, then column) and flow into the fewer columns.
 */
export function place(monitors: Record<string, Monitor>, layout: DashboardLayout | undefined, columns: number): Placed[] {
  const saved = new Map((layout?.instruments ?? []).filter((i) => monitors[i.block]).map((i) => [i.block, i]));
  const narrowed = columns < (layout?.columns ?? DEFAULT_COLUMNS);
  const taken = new Set<string>();
  const free = (c: number, r: number, w: number, h: number) => {
    for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) if (taken.has(`${c + i},${r + j}`)) return false;
    return true;
  };
  const out: Placed[] = [];
  const put = (block: string, c: number, r: number) => {
    const size = saved.get(block)?.size ?? defaultSize(monitors[block].kind);
    const w = Math.min(CELLS[size][0], columns);
    const h = CELLS[size][1];
    // Open from (c, r): wanted cell first, then the rest of the row, then the rows below.
    for (let row = r; ; row++) {
      for (let col = row === r ? Math.min(c, columns - w) : 0; col + w <= columns; col++) {
        if (!free(col, row, w, h)) continue;
        for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) taken.add(`${col + i},${row + j}`);
        out.push({ block, size, col, row, w, h });
        return;
      }
    }
  };
  const order = autoOrder(monitors);
  let withPlace = [...saved.values()].filter((i) => i.at);
  if (narrowed) withPlace = withPlace.sort((a, b) => a.at![1] - b.at![1] || a.at![0] - b.at![0]);
  if (!narrowed) for (const i of withPlace) put(i.block, i.at![0], i.at![1]);
  else for (const i of withPlace) put(i.block, 0, 0);
  const done = new Set(out.map((p) => p.block));
  for (const block of order) if (!done.has(block)) put(block, 0, 0);
  return out;
}

/** `block` moved to a cell (col, row): it is placed first, so whatever it lands on makes way. */
export function move(placed: Placed[], block: string, col: number, row: number, columns: number): DashboardLayout {
  const instruments = placed.map((p) => ({ block: p.block, size: p.size, at: [p.col, p.row] as [number, number] }));
  const mine = instruments.find((i) => i.block === block)!;
  mine.at = [Math.max(0, col), Math.max(0, row)];
  return { columns, instruments: [mine, ...instruments.filter((i) => i !== mine)] };
}

/** `block` changed to `size`, keeping its cell; whatever it now covers makes way. */
export function resize(placed: Placed[], block: string, size: Size, columns: number): DashboardLayout {
  const at = placed.find((p) => p.block === block)!;
  const layout = move(placed, block, at.col, at.row, columns);
  layout.instruments![0].size = size;
  return layout;
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

/** Keep the user's layout for this backend in the browser (until projects exist: #34). `undefined` forgets it. */
export function save(key: string, layout: DashboardLayout | undefined) {
  try {
    if (layout) localStorage.setItem(`paperboat.dashboard:${key}`, JSON.stringify(layout));
    else localStorage.removeItem(`paperboat.dashboard:${key}`);
  } catch {
    /* storage blocked: the layout lasts for the session only */
  }
}
