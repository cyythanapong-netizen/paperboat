import { useContext, useLayoutEffect, useMemo, useRef, useState } from "react";
import type { KeyboardEvent as ReactKeyboardEvent } from "react";
import type { EngineState, Series } from "./engine";
import {
  CELLS,
  DEFAULT_COLUMNS,
  SIZES,
  SIZE_LABEL,
  addDecoration,
  addTab,
  arrange as arrangeLayout,
  editDecoration,
  formatText,
  freshTabName,
  moveDecoration,
  moveInstrument,
  moveToTab,
  removeDecoration,
  removeTab,
  renameTab,
  resizeDecoration,
  resizeInstrument,
  rowsOf,
  tabIsEmpty,
} from "./dashboardLayout";
import type { Monitor, PlacedDeco } from "./dashboardLayout";
import type { DashboardLayout } from "./protocol";
import { Console } from "./Console";
import { WebStation } from "./WebStation";
import { SceneView } from "./SceneView";
import { VAR_DRAG, VarsContext } from "./Variables";
import { RemoveIcon } from "./RemoveIcon";
import { GlideBrackets } from "./Shell";

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

function SwitchInstrument({ name, state, send }: { name: string; state: EngineState; send?: (m: any) => void }) {
  const latest = state.latest[name];
  const [localActive, setLocalActive] = useState<boolean>(true);
  const isDown = typeof latest === "number" ? latest > 0.5 : localActive;

  const toggle = (e: React.MouseEvent) => {
    e.stopPropagation();
    const next = !isDown;
    setLocalActive(next);
    send?.({ type: "input", key: name, down: true });
    setTimeout(() => {
      send?.({ type: "input", key: name, down: false });
    }, 40);
  };

  const handlePointerDown = (e: React.PointerEvent) => {
    e.stopPropagation();
    e.currentTarget.setPointerCapture(e.pointerId);  // the release arrives even off the button
    setLocalActive(true);
    send?.({ type: "input", key: name, down: true });
  };

  const handlePointerUp = (e: React.PointerEvent) => {
    e.stopPropagation();
    setLocalActive(false);
    send?.({ type: "input", key: name, down: false });
  };

  return (
    <figure className="instrument switch-instrument">
      <div className="screen switch-screen">
        <div className="switch-controls">
          <button
            type="button"
            className={`switch-pill${isDown ? " on" : ""}`}
            onClick={toggle}
            title={`Toggle switch (currently ${isDown ? "AUTO / ON" : "MANUAL / OFF"})`}
          >
            <span className="switch-text">{isDown ? "AUTO" : "MAN"}</span>
            <span className="switch-knob" />
          </button>
          <button
            type="button"
            className="switch-momentary-btn"
            onPointerDown={handlePointerDown}
            onPointerUp={handlePointerUp}
            onPointerCancel={handlePointerUp}
            title="Momentary push button: hold down to activate"
          >
            HOLD
          </button>
        </div>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{isDown ? "active (1)" : "idle (0)"}</output>
      </figcaption>
    </figure>
  );
}

/**
 * One instrument: how a monitor shows on the Dashboard is up to its block's `instrument`.
 * Before a run sends it anything it is a greyed placeholder of the same frame (#58), so it
 * can be placed and sized before the first run.
 */
function Instrument({ name, kind, state, send }: { name: string; kind: string; state: EngineState; send?: (m: any) => void }) {
  const latest = state.latest[name];
  const idle = latest === undefined;
  const vector = Array.isArray(latest)
    ? latest.map((v, i) => {
        const component = state.vectorComponents[name]?.[i];
        const label = component?.id ?? String(i);
        return `${label}: ${v.toFixed(4)}${component?.unit ? ` ${component.unit}` : ""}`;
      }).join(", ")
    : undefined;
  if (kind === "switch")
    return <SwitchInstrument name={name} state={state} send={send} />;
  if (kind === "image")
    return (
      <figure className={`instrument image${idle ? " idle" : ""}`}>
        <div className="screen">
          {typeof latest === "string" && latest ? <img src={latest} alt={name} draggable={false} /> : <output>{idle ? "–" : "no image"}</output>}
        </div>
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
          <output>
            {typeof latest === "number"
              ? +latest.toFixed(4)
              : Array.isArray(latest)
                ? `[${vector ?? latest.map((v) => +v.toFixed(4)).join(", ")}]`
                : (latest ?? "–")}
          </output>
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

/** What is being dragged: an instrument (by block id) or a decoration (by its index). */
type Drag = { kind: "instrument"; id: string; dx: number; dy: number } | { kind: "decoration"; id: number; dx: number; dy: number };

/** Text with `{name}` values in it (a monitor block's latest, else a variable's), shown live. */
function Formatted({ text, latest }: { text: string; latest: Record<string, unknown> }) {
  const vars = useContext(VarsContext);
  // A variable's value is the engine's resolved one; until it has resolved, a plain variable's own value.
  const values = useMemo(
    () => ({ ...Object.fromEntries((vars?.list ?? []).filter((v) => !v.expr).map((v) => [v.name, v.value])), ...vars?.values }),
    [vars?.list, vars?.values],
  );
  return (
    <>
      {formatText(text, latest, values).map((p, i) =>
        typeof p === "string" ? (
          p
        ) : (
          <output
            key={i}
            className={`deco-val${p.value === "–" ? " idle" : ""}${p.variable ? " var" : ""}`}
            title={p.shadowed ? `${p.name}: the block (a variable has this name too)` : p.variable ? `${p.name}: a variable` : p.name}
          >
            {p.value}
          </output>
        ),
      )}
    </>
  );
}

/**
 * Text edited where it stands: Enter or a click away keeps it, Escape drops the change.
 * A value chip (`.deco-vars`) may take the focus without ending the edit: a chip clicked
 * or dropped on the text puts `{block}` in at the caret.
 */
function EditText({
  value,
  onDone,
  label,
  inputRef,
}: {
  value: string;
  onDone: (v: string | null) => void;
  label: string;
  inputRef?: React.RefObject<HTMLInputElement | null>;
}) {
  return (
    <input
      className="deco-edit"
      ref={(el) => {
        if (inputRef) inputRef.current = el;
        if (el && !el.dataset.ready) {
          el.dataset.ready = "1";
          el.select(); // the whole text, once: a refocus (after a value chip) keeps the caret
        }
      }}
      autoFocus
      aria-label={label}
      defaultValue={value}
      onBlur={(e) => {
        if ((e.relatedTarget as HTMLElement | null)?.closest(".deco-vars")) return;
        onDone(e.currentTarget.value);
      }}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === "Enter") onDone(e.currentTarget.value);
        if (e.key === "Escape") onDone(null);
      }}
      onDoubleClick={(e) => e.stopPropagation()}
      onDragOver={(e) => e.stopPropagation()}
      onDrop={(e) => {
        // A chip's text lands at the pointer by itself; keep the grid from reading it as a move.
        e.stopPropagation();
        const el = e.currentTarget;
        // A variable from the sheet carries its bare name: it goes in as {name}, at the caret.
        const name = e.dataTransfer.getData(VAR_DRAG);
        if (name) {
          e.preventDefault();
          el.setRangeText(`{${name}}`, el.selectionStart ?? el.value.length, el.selectionEnd ?? el.value.length, "end");
        }
        requestAnimationFrame(() => el.focus());
      }}
    />
  );
}

/**
 * The Dashboard: a grid of equal cells, one instrument per monitor block on the Flow
 * canvas (which decides what exists; adding or removing a block adds or removes its
 * instrument). Each is compact, normal, large or xlarge. Decorations belong to the layout:
 * sections (a titled frame), separators and labels (#65), and tabs, each with its own grid.
 * Where things sit comes from the user's saved layout, else the backend's default
 * (`catalog.dashboard`), else the automatic order. While `arrange` is on, drag an
 * instrument or decoration to a cell (or move it with the arrow keys), pick sizes, add and
 * edit decorations and tabs; the layout is kept in the browser, per backend, until
 * projects can save it (#34).
 */
export function Dashboard({
  state,
  monitors,
  arrange,
  onBlock,
  saved,
  onSave,
  url,
  subscribe,
  send,
  gridOn = true,
}: {
  state: EngineState;
  monitors: Record<string, Monitor>;
  arrange: boolean;
  onBlock: (id: string) => void;
  /** The user's layout for this backend (App keeps it, so renaming a variable can rewrite the text that uses it). */
  saved: DashboardLayout | undefined;
  onSave: (next: DashboardLayout | undefined) => void;
  /** Backend WebSocket URL, for the read-only Station view (web station pages). */
  url?: string;
  subscribe?: (listen: (m: any) => void) => () => void;
  send?: (m: any) => void;
  gridOn?: boolean;
}) {
  const vars = useContext(VarsContext);
  const layout = saved ?? state.dashboard;
  const commit = onSave;

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

  const arranged = useMemo(() => arrangeLayout(all, layout, columns), [all, layout, columns]);
  const { tabs } = arranged;
  const [openTab, setOpenTab] = useState("");
  const tab = tabs.length === 0 ? "" : tabs.includes(openTab) ? openTab : tabs[0];
  const placed = arranged.instruments.filter((p) => p.tab === tab);
  const decos = arranged.decorations.filter((d) => d.tab === tab && !d.hidden);
  const rows = rowsOf(arranged, tab) + (arrange ? 1 : 0);
  const canEdit = arrange && !arranged.narrowed; // on a narrowed screen the cells are not the layout's
  const empty = arranged.instruments.length === 0 && arranged.decorations.length === 0;

  const grid = useRef<HTMLDivElement>(null);
  const drag = useRef<Drag | null>(null);
  const [dragging, setDragging] = useState<Drag | null>(null);
  const [target, setTarget] = useState<{ col: number; row: number } | null>(null);
  const [picked, setPicked] = useState<number | null>(null); // the decoration selected
  const [editing, setEditing] = useState<number | null>(null); // the decoration whose text is edited
  const [renaming, setRenaming] = useState<string | null>(null); // the tab being renamed
  const [sizing, setSizing] = useState<{ index: number; w: number; h: number } | null>(null); // a decoration being resized
  const editor = useRef<HTMLInputElement>(null); // the text input being edited
  const sizingNow = useRef(sizing); // the same, read by the pointer handlers between renders
  const [tabDrop, setTabDrop] = useState<string | null>(null);

  const cellAt = (e: { clientX: number; clientY: number }) => {
    const r = grid.current!.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - r.left) * columns) / (r.width + GAP)),
      row: Math.floor((e.clientY - r.top) / (ROW + GAP)),
    };
  };
  const sizeOf = (d: Drag) => (d.kind === "instrument" ? placed.find((q) => q.block === d.id) : decos.find((q) => q.index === d.id))!;
  const landing = (e: { clientX: number; clientY: number }) => {
    if (!drag.current) return null;
    const c = cellAt(e);
    const it = sizeOf(drag.current);
    return { col: Math.min(Math.max(0, c.col - drag.current.dx), columns - it.w), row: Math.max(0, c.row - drag.current.dy) };
  };
  const stop = () => {
    drag.current = null;
    setDragging(null);
    setTarget(null);
    setTabDrop(null);
  };
  const moveTo = (d: Drag, col: number, row: number) =>
    commit(d.kind === "instrument" ? moveInstrument(arranged, d.id, col, row) : moveDecoration(arranged, d.id, col, row));
  const startDrag = (e: React.DragEvent, d: { kind: Drag["kind"]; id: string | number; col: number; row: number }) => {
    const c = cellAt(e);
    drag.current = { kind: d.kind, id: d.id, dx: Math.max(0, c.col - d.col), dy: Math.max(0, c.row - d.row) } as Drag;
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", String(d.id));
    setDragging(drag.current);
  };
  const arrow = (e: ReactKeyboardEvent, d: Drag, at: { col: number; row: number; w: number }) => {
    const step = ARROWS[e.key];
    if (!step || e.target !== e.currentTarget) return;
    e.preventDefault();
    moveTo(d, Math.min(Math.max(0, at.col + step[0]), columns - at.w), Math.max(0, at.row + step[1]));
  };
  const dragged = dragging ? sizeOf(dragging) : undefined;

  // ---- Decorations ----
  const picks = (index: number) => (e: React.MouseEvent) => {
    e.stopPropagation();
    setPicked(index);
  };
  const decoKey = (e: ReactKeyboardEvent, d: PlacedDeco) => {
    if (e.target !== e.currentTarget) return;
    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      commit(removeDecoration(arranged, d.index));
      setPicked(null);
    } else if (e.key === "Enter" && textual(d)) {
      e.preventDefault();
      setEditing(d.index);
    } else arrow(e, { kind: "decoration", id: d.index, dx: 0, dy: 0 }, d);
  };
  const finishText = (d: PlacedDeco, v: string | null) => {
    setEditing(null);
    if (v !== null && v !== d.text) commit(editDecoration(arranged, d.index, { text: v }));
  };
  // A corner (a separator's end) dragged: the size follows the cell under the pointer.
  const sizePoint = (e: React.PointerEvent, d: PlacedDeco) => {
    const c = cellAt(e);
    sizingNow.current = { index: d.index, w: Math.max(1, c.col - d.col + 1), h: Math.max(1, c.row - d.row + 1) };
    setSizing(sizingNow.current);
  };
  // A value chip clicked: `{block}` goes in at the caret of the text being edited.
  const insertValue = (name: string) => {
    const el = editor.current;
    if (!el) return;
    el.setRangeText(`{${name}}`, el.selectionStart ?? el.value.length, el.selectionEnd ?? el.value.length, "end");
    el.focus();
  };
  const textual = (d: PlacedDeco | undefined) => d !== undefined && (d.kind === "label" || d.kind === "section");
  // A variable dragged from the Variables sheet onto a label or section title shows its
  // value there: `{name}` goes at the end of the text (at the caret while it is edited).
  const [varOver, setVarOver] = useState<number | null>(null);
  const takesVar = (e: React.DragEvent, d: PlacedDeco) => textual(d) && !arranged.narrowed && e.dataTransfer.types.includes(VAR_DRAG);
  const varDrop = (d: PlacedDeco) => ({
    onDragOver: (e: React.DragEvent) => {
      if (!takesVar(e, d)) return;
      e.preventDefault();
      e.stopPropagation();
      e.dataTransfer.dropEffect = "link";
      setVarOver(d.index);
    },
    onDragLeave: (e: React.DragEvent) => {
      if (!e.currentTarget.contains(e.relatedTarget as Node | null)) setVarOver(null);
    },
    onDrop: (e: React.DragEvent) => {
      setVarOver(null);
      if (!takesVar(e, d)) return;
      e.preventDefault();
      e.stopPropagation();
      const name = e.dataTransfer.getData(VAR_DRAG);
      if (name && editing !== d.index) commit(editDecoration(arranged, d.index, { text: `${d.text}${d.text && !d.text.endsWith(" ") ? " " : ""}{${name}}` }));
    },
  });
  const editingDeco = editing === null ? undefined : arranged.decorations.find((d) => d.index === editing);
  const shown = (d: PlacedDeco): PlacedDeco => (sizing?.index === d.index ? { ...d, ...(d.kind === "hline" ? { w: sizing.w } : d.kind === "vline" ? { h: sizing.h } : { w: sizing.w, h: sizing.h }) } : d);

  // A tab's name, edited where it stands. Enter or leaving keeps it; Esc, or an empty
  // name, drops the edit; a name another tab has stays open, marked, until it's changed.
  const renameField = (t: string) => (
    <input
      className="deco-edit tab-edit"
      autoFocus
      aria-label={`Rename tab ${t}`}
      defaultValue={t}
      ref={(el) => {
        if (el && !el.dataset.ready) {
          el.dataset.ready = "1";
          el.select();
        }
      }}
      onChange={(e) => {
        const v = e.currentTarget.value.trim();
        const taken = v !== t && tabs.includes(v);
        e.currentTarget.setAttribute("aria-invalid", String(taken));
        e.currentTarget.title = taken ? `A tab is already called ${v}` : "";
      }}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === "Escape") setRenaming(null);
        if (e.key === "Enter") finishRename(t, e.currentTarget.value, true);
      }}
      onBlur={(e) => finishRename(t, e.currentTarget.value, false)}
    />
  );
  const finishRename = (t: string, raw: string, stay: boolean) => {
    const v = raw.trim();
    if (v && v !== t && tabs.includes(v)) {
      if (!stay) setRenaming(null); // leaving with a taken name drops the edit
      return;
    }
    setRenaming(null);
    if (v && v !== t) {
      commit(renameTab(arranged, t, v));
      if (tab === t) setOpenTab(v);
    }
  };

  return (
    <div className="dashboard" ref={root}>
      {arrange && (
        <div className="dash-bar">
          {arranged.narrowed ? (
            <span>The screen is narrower than this layout: widen it to arrange.</span>
          ) : (
            <span>Drag an instrument or decoration to a cell (or focus it and use the arrow keys); pick a size; double-click text to edit; put a value in it with {"{block}"}, or drag a variable from the Variables sheet onto it.</span>
          )}
          <span className="dash-add" role="group" aria-label="Add">
            <span className="lbl">Add</span>
            {(
              [
                ["section", "Section", "A titled frame round a group of cells"],
                ["label", "Label", "A heading or a note; {block} in it shows that block's value"],
                ["console", "Console", "What the backend logged, in a frame"],
                ["station", "Station", "A read-only view of the Station"],
                ["hline", "Separator —", "A horizontal rule between rows"],
                ["vline", "Separator |", "A vertical rule between columns"],
              ] as const
            ).map(([kind, text, title]) => (
              <button
                key={kind}
                className="key small"
                disabled={!canEdit}
                title={title}
                onClick={() => {
                  commit(addDecoration(arranged, kind, tab));
                  setPicked(arranged.decorations.length);
                }}
              >
                {text}
              </button>
            ))}
            <button
              className="key small"
              disabled={!canEdit}
              title="Another page of the Board"
              onClick={() => {
                const name = freshTabName(tabs.length ? tabs : ["Main"]);
                commit(addTab(arranged, name));
                setOpenTab(name);
                setRenaming(name); // name it as it's made
              }}
            >
              + tab
            </button>
          </span>
          <button className="key small" disabled={!saved} title="Back to the backend's layout, or the automatic one" onClick={() => commit(undefined)}>
            Reset layout
          </button>
        </div>
      )}
      {arrange && textual(editingDeco) && (
        <div className="deco-vars" role="group" aria-label="Insert a value">
          <span className="lbl">Value</span>
          {[...Object.keys(all).map((n) => [n, false] as const), ...(vars?.list ?? []).filter((v) => !(v.name in all)).map((v) => [v.name, true] as const)].map(([n, isVar]) => (
            <button
              key={n}
              className={`chip${isVar ? " var" : ""}`}
              draggable
              title={`${isVar ? "Variable: click" : "Click"} to put {${n}} in the text at the caret, or drag it in`}
              onDragStart={(e) => {
                e.stopPropagation();
                e.dataTransfer.effectAllowed = "copy";
                e.dataTransfer.setData("text/plain", `{${n}}`);
              }}
              onClick={() => insertValue(n)}
            >
              {n}
            </button>
          ))}
        </div>
      )}
      {tabs.length >= 2 && (
        <div className="dash-tabs" role="tablist" aria-label="Board pages">
          {tabs.map((t) => (
            <span
              key={t}
              className={`dash-tab${t === tab ? " on" : ""}${tabDrop === t ? " drop" : ""}`}
              onDragOver={(e) => {
                if (drag.current?.kind !== "instrument") return;
                e.preventDefault();
                setTabDrop(t);
              }}
              onDragLeave={() => setTabDrop(null)}
              onDrop={(e) => {
                e.preventDefault();
                if (drag.current?.kind === "instrument") commit(moveToTab(arranged, drag.current.id, t));
                stop();
              }}
            >
              {renaming === t ? (
                renameField(t)
              ) : (
                <button
                  role="tab"
                  className="opt"
                  aria-selected={t === tab}
                  title={`Double-click or F2 to rename${canEdit ? "; drop an instrument here to move it to this tab" : ""}`}
                  onClick={() => setOpenTab(t)}
                  onDoubleClick={() => setRenaming(t)}
                  onKeyDown={(e) => {
                    if (e.key !== "F2") return;
                    e.preventDefault();
                    setRenaming(t);
                  }}
                >
                  {t}
                </button>
              )}
              {canEdit && tabIsEmpty(arranged, t) && (
                <button className="tab-remove" aria-label={`Remove tab ${t}`} title="Remove this empty tab" onClick={() => commit(removeTab(arranged, t))}>
                  ×
                </button>
              )}
            </span>
          ))}
          <GlideBrackets />
        </div>
      )}
      {empty && (
        <p className="empty">Add a Graph, Display or Image view block on the Flow page: its instrument shows here, and a run fills it.</p>
      )}
      {(!empty || arrange) && (
        <div
          ref={grid}
          className={`dash-grid${arrange ? " arranging" : ""}`}
          style={{ "--cols": columns, "--row": `${ROW}px`, "--gap": `${GAP}px` } as React.CSSProperties}
          onClick={() => setPicked(null)}
          onDragOver={(e) => {
            if (!drag.current) return;
            e.preventDefault();
            setTarget(landing(e));
          }}
          onDrop={(e) => {
            const at = landing(e);
            if (at && drag.current) moveTo(drag.current, at.col, at.row);
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
          {decos.map((raw) => {
            const d = shown(raw);
            const line = d.kind === "hline" || d.kind === "vline";
            const mine = dragging?.kind === "decoration" && dragging.id === d.index;
            return (
              <div
                key={d.index}
                className={`deco deco-${d.kind}${d.kind === "label" ? ` ${d.style}` : ""}${picked === d.index ? " picked" : ""}${mine ? " dragging" : ""}${varOver === d.index ? " var-over" : ""}`}
                style={{ gridColumn: `${d.col + 1} / span ${d.w}`, gridRow: `${d.row + 1} / span ${d.h}` }}
                draggable={canEdit && editing !== d.index && !sizing}
                tabIndex={canEdit ? 0 : undefined}
                role={d.kind === "label" || d.kind === "section" ? undefined : "separator"}
                aria-orientation={d.kind === "vline" ? "vertical" : d.kind === "hline" ? "horizontal" : undefined}
                onClick={canEdit ? picks(d.index) : undefined}
                onDoubleClick={() => canEdit && textual(d) && setEditing(d.index)}
                onKeyDown={(e) => canEdit && decoKey(e, d)}
                onDragStart={(e) => (sizing ? e.preventDefault() : startDrag(e, { kind: "decoration", id: d.index, col: d.col, row: d.row }))}
                onDragEnd={stop}
                {...varDrop(d)}
              >
                {d.kind === "section" && (
                  <span className="deco-title">
                    {editing === d.index ? <EditText value={d.text} label="Section title" inputRef={editor} onDone={(v) => finishText(d, v)} /> : <Formatted text={d.text} latest={state.latest} />}
                  </span>
                )}
                {d.kind === "label" && (editing === d.index ? <EditText value={d.text} label="Label text" inputRef={editor} onDone={(v) => finishText(d, v)} /> : <span><Formatted text={d.text} latest={state.latest} /></span>)}
                {d.kind === "console" && (
                  <div className="deco-console">
                    <Console logs={state.logs} onBlock={onBlock} />
                  </div>
                )}
                {d.kind === "station" && (
                  <div className="deco-station">
                    {state.stationView.view === "web" && state.stationView.page && url && subscribe && send ? (
                      <WebStation backend={url} page={state.stationView.page} state={state} subscribe={subscribe} send={send} onStation={() => {}} />
                    ) : state.stationView.view === "graph" ? (
                      <div className="deco-station-fallback">Station graph view is not available in the Board.</div>
                    ) : (
                      <SceneView view={state.view} grid={gridOn} entities={state.entities} />
                    )}
                  </div>
                )}
                {canEdit && picked === d.index && (
                  <>
                    <button
                      className="remove deco-remove nodrag"
                      aria-label="Remove"
                      title="Remove (or press Delete)"
                      onClick={(e) => {
                        e.stopPropagation();
                        commit(removeDecoration(arranged, d.index));
                        setPicked(null);
                      }}
                    >
                      <RemoveIcon />
                    </button>
                    {d.kind === "label" && (
                      <span className="switch deco-style" role="group" aria-label="Label style" onClick={(e) => e.stopPropagation()}>
                        {(["heading", "note"] as const).map((s) => (
                          <button key={s} className={`opt${d.style === s ? " on" : ""}`} aria-pressed={d.style === s} onClick={() => commit(editDecoration(arranged, d.index, { style: s }))}>
                            {s}
                          </button>
                        ))}
                        <GlideBrackets />
                      </span>
                    )}
                    <span
                      className={`deco-grip${line ? ` grip-${d.kind}` : ""}`}
                      title={line ? "Drag to change its length" : "Drag to resize"}
                      draggable={false}
                      onMouseDown={(e) => e.preventDefault()}
                      onClick={(e) => e.stopPropagation()}
                      onPointerDown={(e) => {
                        e.stopPropagation();
                        e.currentTarget.setPointerCapture(e.pointerId);
                        sizePoint(e, d);
                      }}
                      onPointerMove={(e) => sizingNow.current && e.buttons && sizePoint(e, d)}
                      onPointerUp={(e) => {
                        const last = sizingNow.current;
                        if (last) commit(resizeDecoration(arranged, d.index, last.w, last.h));
                        sizingNow.current = null;
                        setSizing(null);
                        e.currentTarget.releasePointerCapture(e.pointerId);
                      }}
                    />
                  </>
                )}
              </div>
            );
          })}
          {placed.map((p) => (
            <div
              key={p.block}
              className={`cell size-${p.size}${dragging?.kind === "instrument" && dragging.id === p.block ? " dragging" : ""}`}
              style={{ gridColumn: `${p.col + 1} / span ${p.w}`, gridRow: `${p.row + 1} / span ${p.h}` }}
              draggable={canEdit}
              tabIndex={canEdit ? 0 : undefined}
              onKeyDown={(e) => canEdit && arrow(e, { kind: "instrument", id: p.block, dx: 0, dy: 0 }, p)}
              onDragStart={(e) => startDrag(e, { kind: "instrument", id: p.block, col: p.col, row: p.row })}
              onDragEnd={stop}
            >
              <Instrument name={p.block} kind={all[p.block].kind} state={state} send={send} />
              {canEdit && (
                <div className="switch cell-tools" role="group" aria-label={`Size of ${p.block}`}>
                  {SIZES.map((s) => (
                    <button
                      key={s}
                      className={`opt${p.size === s ? " on" : ""}`}
                      title={`${s}: ${CELLS[s][0]}×${CELLS[s][1]} cells`}
                      aria-label={s}
                      aria-pressed={p.size === s}
                      onClick={() => commit(resizeInstrument(arranged, p.block, s))}
                    >
                      {SIZE_LABEL[s]}
                    </button>
                  ))}
                  <GlideBrackets />
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
