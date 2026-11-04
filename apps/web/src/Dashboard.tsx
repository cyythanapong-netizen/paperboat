import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import type { KeyboardEvent as ReactKeyboardEvent } from "react";
import type { EngineState, Series } from "./engine";
import { CELLS, DEFAULT_COLUMNS, SIZES, SIZE_LABEL, loadSaved, move, place, resize, save } from "./dashboardLayout";
import type { Monitor, Placed } from "./dashboardLayout";

export type { Monitor } from "./dashboardLayout";

const GAP = 12; // px between cells
const ROW = 116; // px, one cell's height
const MIN_CELL = 96; // px: a screen too narrow for the layout's columns gets fewer

const NO_SERIES: Series = { t: [], v: [] };

function Plot({ name, series, idle }: { name: string; series: Series; idle: boolean }) {
  const W = 300;
  const H = 100;
  const tMax = Math.max(...series.t, 1e-9);
  const vMin = Math.min(...series.v, 0);
  const vMax = Math.max(...series.v, 0);
  const span = vMax - vMin || 1;
  const points = series.t.map((t, i) => `${(t / tMax) * W},${H - ((series.v[i] - vMin) / span) * H}`).join(" ");
  const last = series.v[series.v.length - 1];
  return (
    <figure className={`instrument graph${idle ? " idle" : ""}`}>
      <div className="screen">
        <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
          <line className="zero" x1={0} x2={W} y1={H - (-vMin / span) * H} y2={H - (-vMin / span) * H} />
          <polyline className="trace" points={points} fill="none" vectorEffect="non-scaling-stroke" />
        </svg>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{last === undefined ? (idle ? "graph" : "–") : last.toFixed(3)}</output>
      </figcaption>
    </figure>
  );
}

/**
 * One instrument: how a monitor shows on the Dashboard is up to its block's `instrument`.
 * Before a run sends it anything it is a greyed placeholder of the same frame (#58), so it
 * can be placed and sized before the first run.
 */
function Instrument({ name, kind, state }: { name: string; kind: string; state: EngineState }) {
  const latest = state.latest[name];
  const idle = latest === undefined;
  if (kind === "image")
    return (
      <figure className={`instrument image${idle ? " idle" : ""}`}>
        <div className="screen">{typeof latest === "string" && <img src={latest} alt={name} draggable={false} />}</div>
        <figcaption>
          <span>{name}</span>
          <span>image</span>
        </figcaption>
      </figure>
    );
  if (kind === "display")
    return (
      <figure className={`instrument display${idle ? " idle" : ""}`}>
        <div className="screen">
          <output>{typeof latest === "number" ? +latest.toFixed(4) : (latest ?? "–")}</output>
        </div>
        <figcaption>
          <span>{name}</span>
          <span>display</span>
        </figcaption>
      </figure>
    );
  return <Plot name={name} series={state.signals[name] ?? NO_SERIES} idle={idle} />;
}

const ARROWS: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };

/**
 * The Dashboard: a grid of equal cells, one instrument per monitor block on the Flow
 * canvas (which decides what exists; adding or removing a block adds or removes its
 * instrument). Each is compact, normal, large or xlarge (1x1, 2x1, 2x2, 3x3 cells). Where one sits
 * comes from the user's saved layout, else the backend's default (`catalog.dashboard`),
 * else the automatic order. While `arrange` is on, drag an instrument to a cell (or move
 * it with the arrow keys) and pick its size; the layout is kept in the browser, per
 * backend, until projects can save it (#34).
 */
export function Dashboard({ state, monitors, arrange }: { state: EngineState; monitors: Record<string, Monitor>; arrange: boolean }) {
  const storeKey = state.engine || "default";
  const [saved, setSaved] = useState(() => loadSaved(storeKey));
  useEffect(() => setSaved(loadSaved(storeKey)), [storeKey]);
  const layout = saved ?? state.dashboard;
  const commit = (next: typeof saved) => {
    setSaved(next);
    save(storeKey, next);
  };

  // Every monitor block on the Flow canvas has an instrument, and only those (#58): what a
  // run reported for a block that's gone since doesn't keep its tile.
  const all = monitors;

  const root = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(0);
  useLayoutEffect(() => {
    const el = root.current;
    if (!el) return;
    const watch = new ResizeObserver(() => setWidth(el.clientWidth));
    watch.observe(el);
    setWidth(el.clientWidth);
    return () => watch.disconnect();
  }, []);
  const configured = layout?.columns ?? DEFAULT_COLUMNS;
  const columns = width ? Math.max(1, Math.min(configured, Math.floor((width + GAP) / (MIN_CELL + GAP)))) : configured;

  const placed = useMemo(() => place(all, layout, columns), [all, layout, columns]);
  const rows = Math.max(0, ...placed.map((p) => p.row + p.h)) + (arrange ? 1 : 0);

  const grid = useRef<HTMLDivElement>(null);
  const drag = useRef<{ block: string; dx: number; dy: number } | null>(null);
  const [dragging, setDragging] = useState<string | null>(null);
  const [target, setTarget] = useState<{ col: number; row: number } | null>(null);
  const cellAt = (e: { clientX: number; clientY: number }) => {
    const r = grid.current!.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - r.left) * columns) / (r.width + GAP)),
      row: Math.floor((e.clientY - r.top) / (ROW + GAP)),
    };
  };
  const landing = (e: { clientX: number; clientY: number }) => {
    if (!drag.current) return null;
    const c = cellAt(e);
    const p = placed.find((q) => q.block === drag.current!.block)!;
    return { col: Math.min(Math.max(0, c.col - drag.current.dx), columns - p.w), row: Math.max(0, c.row - drag.current.dy) };
  };
  const stop = () => {
    drag.current = null;
    setDragging(null);
    setTarget(null);
  };
  const moveTo = (block: string, col: number, row: number) => commit(move(placed, block, col, row, columns));
  const onKey = (e: ReactKeyboardEvent, p: Placed) => {
    const d = ARROWS[e.key];
    if (!d || e.target !== e.currentTarget) return;
    e.preventDefault();
    moveTo(p.block, Math.min(Math.max(0, p.col + d[0]), columns - p.w), Math.max(0, p.row + d[1]));
  };
  const dragged = placed.find((p) => p.block === dragging);

  return (
    <div className="dashboard" ref={root}>
      {arrange && (
        <div className="dash-bar">
          <span>Drag an instrument to a cell (or focus it and use the arrow keys); pick its size.</span>
          <button className="key small" disabled={!saved} title="Back to the backend's layout, or the automatic one" onClick={() => commit(undefined)}>
            Reset layout
          </button>
        </div>
      )}
      {placed.length === 0 && (
        <p className="empty">Add a Graph, Display or Image view block on the Flow page: its instrument shows here, and a run fills it.</p>
      )}
      {placed.length > 0 && (
        <div
          ref={grid}
          className={`dash-grid${arrange ? " arranging" : ""}`}
          style={{ "--cols": columns, "--row": `${ROW}px`, "--gap": `${GAP}px` } as React.CSSProperties}
          onDragOver={(e) => {
            if (!drag.current) return;
            e.preventDefault();
            setTarget(landing(e));
          }}
          onDrop={(e) => {
            const at = landing(e);
            if (at && drag.current) moveTo(drag.current.block, at.col, at.row);
            stop();
          }}
        >
          {arrange &&
            Array.from({ length: rows * columns }, (_, i) => (
              <div key={i} className="slot" style={{ gridColumn: (i % columns) + 1, gridRow: Math.floor(i / columns) + 1 }} />
            ))}
          {target && dragged && (
            <div className="drop" style={{ gridColumn: `${target.col + 1} / span ${dragged.w}`, gridRow: `${target.row + 1} / span ${dragged.h}` }} />
          )}
          {placed.map((p) => (
            <div
              key={p.block}
              className={`cell size-${p.size}${dragging === p.block ? " dragging" : ""}`}
              style={{ gridColumn: `${p.col + 1} / span ${p.w}`, gridRow: `${p.row + 1} / span ${p.h}` }}
              draggable={arrange}
              tabIndex={arrange ? 0 : undefined}
              onKeyDown={(e) => arrange && onKey(e, p)}
              onDragStart={(e) => {
                const c = cellAt(e);
                drag.current = { block: p.block, dx: Math.max(0, c.col - p.col), dy: Math.max(0, c.row - p.row) };
                e.dataTransfer.effectAllowed = "move";
                e.dataTransfer.setData("text/plain", p.block);
                setDragging(p.block);
              }}
              onDragEnd={stop}
            >
              <Instrument name={p.block} kind={all[p.block].kind} state={state} />
              {arrange && (
                <div className="switch cell-tools" role="group" aria-label={`Size of ${p.block}`}>
                  {SIZES.map((s) => (
                    <button
                      key={s}
                      className={`opt${p.size === s ? " on" : ""}`}
                      title={`${s}: ${CELLS[s][0]}×${CELLS[s][1]} cells`}
                      aria-label={s}
                      aria-pressed={p.size === s}
                      onClick={() => commit(resize(placed, p.block, s, columns))}
                    >
                      {SIZE_LABEL[s]}
                    </button>
                  ))}
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
