import { useLayoutEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as ReactPointerEvent } from "react";
import { useDismiss } from "@paperboat/shared/components";
import type { BlockDef, FlowType, ObjectDef, ParamDef, PortDef } from "./protocol";
import { byFlow, flowLabel, flowsLabel, notHere } from "./flowTypes.ts";
import { GlideBrackets } from "./Shell";
import { readStored, writeStored } from "./storage.ts";

/**
 * The block and object browser (#50, DESIGN.md): every block or station object is a card
 * that says what it is and does. Two ways in: the page's Add key opens the collection as a
 * floating window; `/` or a double-click on the canvas opens a quick add at the cursor.
 * Cards can be dragged onto the canvas (the Flow decides what the drop does).
 */
export type Item =
  | { kind: "block"; def: BlockDef }
  | { kind: "object"; def: ObjectDef; linked?: BlockDef };

/** What is being dragged out of the browser, if anything (HTML drag data can't be read while hovering). */
export const dragging: { current: Item | null } = { current: null };
export const DRAG_TYPE = "application/x-paperboat-item";

const stepPorts = (d: BlockDef) => [...d.inputs, ...d.outputs].some((p) => p.type === "step");

/** A block's kind in words, as its head on the canvas says it. */
export function blockKind(d: BlockDef): string {
  if (d.forms?.length) return d.forms.map((f) => f.name).join(" or ");
  const step = stepPorts(d);
  if (step && !d.inputs.some((p) => p.type === "step")) return "hat";
  if (step) return "step";
  const k = d.kind === "function" ? "fn" : d.kind;
  return d.feedthrough === false ? `${k} · no feedthrough` : k;
}

function card(item: Item) {
  if (item.kind === "block") {
    const d = item.def;
    return { type: d.type, label: d.label, group: d.group ?? "", kind: blockKind(d), filter: stepPorts(d) ? "step" : d.kind === "function" ? "fn" : d.kind, description: d.description ?? "", inputs: d.inputs, outputs: d.outputs, params: d.params, step: stepPorts(d) };
  }
  // An object's ports are its linked block's: how a flow pushes on it and reads it.
  const d = item.def;
  const role = d.links.length ? `${d.role} · links ${d.links.join(", ")}` : d.role;
  return { type: d.type, label: d.label, group: d.group ?? "", kind: role, filter: d.role, description: d.description ?? "", inputs: item.linked?.inputs ?? [], outputs: item.linked?.outputs ?? [], params: d.props, step: false };
}

/** Best matches first: the name, then words in it, then the description, ports and params. */
export function search(items: Item[], query: string): Item[] {
  const q = query.trim().toLowerCase();
  if (!q) return items;
  const score = (it: Item) => {
    const c = card(it);
    const name = `${c.label} ${c.type}`.toLowerCase();
    if (name.startsWith(q)) return 0;
    if (name.split(/[\s\-_]+/).some((w) => w.startsWith(q))) return 1;
    if (name.includes(q)) return 2;
    const rest = [c.description, c.group, c.kind, ...c.inputs.map((p) => p.id), ...c.outputs.map((p) => p.id), ...c.params.map((p) => p.id)].join(" ").toLowerCase();
    return rest.includes(q) ? 3 : 9;
  };
  return items
    .map((it) => [score(it), it] as const)
    .filter(([s]) => s < 9)
    .sort((a, b) => a[0] - b[0])
    .map(([, it]) => it);
}

const fmt = (p: ParamDef) => (p.type === "boolean" ? (p.default ? "on" : "off") : String(p.default ?? ""));

function Ports({ inputs, outputs }: { inputs: PortDef[]; outputs: PortDef[] }) {
  const port = (p: PortDef, side: "in" | "out") => (
    <span key={p.id} className={`card-port ${side}${p.type === "step" ? " step" : ""}`}>
      {side === "out" && p.id}
      {side === "out" && p.unit && <span className="unit"> {p.unit}</span>}
      <i />
      {side === "in" && p.id}
      {side === "in" && p.unit && <span className="unit"> {p.unit}</span>}
    </span>
  );
  return (
    <div className="card-ports">
      <span className="ins">{inputs.length ? inputs.map((p) => port(p, "in")) : <span className="none">–</span>}</span>
      <span className="arrow">→</span>
      <span className="outs">{outputs.length ? outputs.map((p) => port(p, "out")) : <span className="none">–</span>}</span>
    </div>
  );
}

/** A card in the browser; `off`: why it can't go on this sheet (#9), shown muted, not draggable. */
function Card({ item, count, onAdd, place, off }: { item: Item; count: number; onAdd: () => void; place: string; off?: string }) {
  const c = card(item);
  return (
    <div
      className={`card${c.step ? " step" : ""}${off ? " off" : ""}`}
      draggable={!off}
      aria-disabled={off ? true : undefined}
      onDragStart={(e) => {
        if (off) return e.preventDefault();
        dragging.current = item;
        e.dataTransfer.setData(DRAG_TYPE, c.type);
        e.dataTransfer.effectAllowed = "copy";
        // While a card is out, the window fades and lets the pointer through: drop anywhere,
        // under it too. (After this event, or the browser cancels the drag.)
        setTimeout(() => document.body.classList.add("dragging-card"));
      }}
      onDragEnd={() => {
        dragging.current = null;
        document.body.classList.remove("dragging-card");
      }}
      title={off ?? `Drag onto the ${place}, or Add`}
    >
      <div className="card-head">
        <b>{c.label}</b>
        <span className="lbl">{c.kind}</span>
      </div>
      {c.description && <p className="card-desc">{c.description}</p>}
      <Ports inputs={c.inputs} outputs={c.outputs} />
      {c.params.length > 0 && (
        <div className="card-params">
          {c.params.map((p) => (
            <span key={p.id}>
              {p.id} <b>{fmt(p)}</b>
              {"unit" in p && p.unit && <span className="unit"> {p.unit}</span>}
            </span>
          ))}
        </div>
      )}
      <div className="card-foot">
        <span className="lbl">{off ?? (count ? `${count} on the ${place}` : "")}</span>
        <button className="key small" onClick={onAdd} disabled={!!off}>
          Add
        </button>
      </div>
    </div>
  );
}

/** A remembered spot for a floating window, kept in this browser when it can be. */
function usePlace(key: string, fallback: { x: number; y: number }) {
  const [at, setAt] = useState(
    () => readStored(key, (v): v is { x: number; y: number } => !!v && typeof (v as { x: unknown }).x === "number") ?? fallback,
  );
  const keep = (v: { x: number; y: number }) => {
    setAt(v);
    writeStored(key, v);
  };
  return [at, keep] as const;
}

/**
 * The collection, as a floating window over the page: search, a kind filter, packages on
 * the left, cards on the right. Move it by its head; it stays where it was put. With `flow`
 * (the open sheet's type, #9), blocks that don't work in it are kept apart at the end, muted,
 * each saying where it works.
 */
export function BrowserWindow({
  title,
  items: all,
  counts,
  place,
  flow,
  mixed,
  onAdd,
  onClose,
}: {
  title: string;
  items: Item[];
  counts: Record<string, number>;
  /** Where cards go: "canvas" or "station". */
  place: string;
  /** The open sheet's flow type: only blocks that work in it can be added. */
  flow?: FlowType;
  /** A signal/step sheet admits both layers: step blocks on a Signal flow, signal blocks on a Step flow. */
  mixed?: boolean;
  onAdd: (item: Item) => void;
  onClose: () => void;
}) {
  const { fits: items, elsewhere } = useMemo(
    () => (flow ? byFlow(all, flow, mixed) : { fits: all, elsewhere: [] as Item[] }),
    [all, flow, mixed],
  );
  const [query, setQuery] = useState("");
  const [kind, setKind] = useState("all");
  const [group, setGroup] = useState("");
  const [at, setAt] = usePlace(`paperboat.browser.${place}`, { x: Math.max(16, window.innerWidth - 900), y: 120 });
  const box = useRef<HTMLDivElement>(null);
  // The window floats over the page, but it opens inside the panel it belongs to: no covering
  // the Variables tab or running off the edge of the Flow.
  const clamped = useRef(false);
  useLayoutEffect(() => {
    if (clamped.current) return;
    clamped.current = true;
    const el = box.current;
    const host = document.querySelector(place === "canvas" ? ".panel-flow" : ".panel-station");
    if (!el || !host) return;
    const r = el.getBoundingClientRect();
    const h = host.getBoundingClientRect();
    const x = Math.max(8, Math.min(at.x, h.right - r.width - 8));
    const y = Math.max(8, Math.min(at.y, h.bottom - r.height - 8));
    if (x !== at.x || y !== at.y) setAt({ x, y });
  }, [at, place, setAt]);

  const kinds = useMemo(() => ["all", ...new Set(items.map((i) => card(i).filter))], [items]);
  // Every package the catalog has, including one whose blocks only work in another flow type:
  // otherwise an installed toolkit disappears from the list on a sheet it can't join.
  const groups = useMemo(() => [...new Set(all.map((i) => card(i).group))], [all]);
  const found = search(items, query).filter((i) => kind === "all" || card(i).filter === kind);
  const shown = found.filter((i) => !group || card(i).group === group);
  const byGroup = groups.map((g) => [g, shown.filter((i) => card(i).group === g)] as const).filter(([, list]) => list.length);
  // What this sheet can't take, under the same search and filters.
  const awayAll = search(elsewhere, query).filter((i) => kind === "all" || card(i).filter === kind);
  const away = awayAll.filter((i) => !group || card(i).group === group);

  // Move by the head; keep it on screen.
  const drag = (e: ReactPointerEvent) => {
    if ((e.target as HTMLElement).closest("button, input")) return;
    const start = { x: e.clientX - at.x, y: e.clientY - at.y };
    const move = (m: PointerEvent) =>
      setAt({
        x: Math.min(Math.max(0, m.clientX - start.x), window.innerWidth - 120),
        y: Math.min(Math.max(0, m.clientY - start.y), window.innerHeight - 60),
      });
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  };
  useDismiss(box, onClose);

  return (
    <div className="browser" ref={box} style={{ left: at.x, top: at.y }} role="dialog" aria-label={title}>
      <div className="browser-head" onPointerDown={drag}>
        <h2>{title}</h2>
        <span className="well search">
          ⌕
          <input autoFocus placeholder="Search names, ports, params…" value={query} onChange={(e) => setQuery(e.target.value)} />
        </span>
        <div className="switch" role="radiogroup" aria-label="Kind">
          {kinds.map((k) => (
            <button key={k} role="radio" aria-checked={kind === k} className={`opt${kind === k ? " on" : ""}`} onClick={() => setKind(k)}>
              {k}
            </button>
          ))}
          <GlideBrackets />
        </div>
        <span className="lbl found">{found.length + awayAll.length} {found.length + awayAll.length === 1 ? "match" : "matches"}</span>
        <button className="key small" title="Close (Esc)" onClick={onClose}>
          Close
        </button>
      </div>
      <div className="browser-body">
        <nav className="browser-groups" aria-label="Packages">
          {["", ...groups].map((g) => {
            const fits = found.filter((i) => !g || card(i).group === g).length;
            const off = awayAll.filter((i) => !g || card(i).group === g).length;
            const n = fits + off;
            return (
              <button
                key={g || "all"}
                className={group === g ? "current" : undefined}
                title={fits === 0 && off > 0 && flow ? `Works only in a ${flowLabel(flow)}` : undefined}
                onClick={() => setGroup(g)}
              >
                <span>{g ? g[0].toUpperCase() + g.slice(1) : "All"}</span>
                <span className="lbl">{n}</span>
              </button>
            );
          })}
        </nav>
        <div className="browser-cards">
          {byGroup.map(([g, list]) => (
            <section key={g || "none"}>
              <h3 className="lbl">{g ? g[0].toUpperCase() + g.slice(1) : "Other"}</h3>
              <div className="cards">
                {list.map((it) => (
                  <Card key={it.def.type} item={it} count={counts[it.def.type] ?? 0} place={place} onAdd={() => onAdd(it)} />
                ))}
              </div>
            </section>
          ))}
          {shown.length === 0 && away.length === 0 && <p className="empty">{query ? `Nothing matches “${query}”` : "Nothing here"}{flow ? ` in this ${flowLabel(flow)}` : ""}.</p>}
          {flow && away.length > 0 && (
            <details className="browser-elsewhere" open={shown.length === 0}>
              <summary className="lbl">
                Not in this {flowLabel(flow)} · {away.length}
              </summary>
              <div className="cards">
                {away.map((it) => (
                  <Card key={it.def.type} item={it} count={counts[it.def.type] ?? 0} place={place} onAdd={() => {}} off={it.kind === "block" ? notHere(it.def, flow) : undefined} />
                ))}
              </div>
            </details>
          )}
        </div>
      </div>
    </div>
  );
}

/**
 * Quick add at the cursor: a search with the best matches; arrows choose, Enter adds at
 * that spot, Escape (or a click elsewhere) closes.
 */
export function QuickAdd({
  items: all,
  flow,
  mixed,
  at,
  onPick,
  onClose,
}: {
  items: Item[];
  /** The open sheet's flow type (#9): blocks that don't work in it are only named, muted, below. */
  flow?: FlowType;
  /** A signal/step sheet admits both layers. */
  mixed?: boolean;
  at: { x: number; y: number };
  onPick: (item: Item) => void;
  onClose: () => void;
}) {
  const [query, setQuery] = useState("");
  const [i, setI] = useState(0);
  const box = useRef<HTMLDivElement>(null);
  const { fits: items, elsewhere } = useMemo(
    () => (flow ? byFlow(all, flow, mixed) : { fits: all, elsewhere: [] as Item[] }),
    [all, flow, mixed],
  );
  const list = search(items, query).slice(0, 7);
  const away = query.trim() && flow ? search(elsewhere, query).slice(0, 3) : [];
  const pick = list[Math.min(i, list.length - 1)];
  // Stay on screen.
  const [pos, setPos] = useState(at);
  useLayoutEffect(() => {
    const el = box.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setPos({ x: Math.min(at.x, window.innerWidth - r.width - 8), y: Math.min(at.y, window.innerHeight - r.height - 8) });
  }, [at]);
  useDismiss(box, onClose);
  const onKey = (e: KeyboardEvent) => {
    e.stopPropagation(); // typing here is not input for the backend
    if (e.key === "ArrowDown") (e.preventDefault(), setI((v) => Math.min(v + 1, list.length - 1)));
    else if (e.key === "ArrowUp") (e.preventDefault(), setI((v) => Math.max(v - 1, 0)));
    else if (e.key === "Enter" && pick) (e.preventDefault(), onPick(pick));
    else if (e.key === "Escape") (e.preventDefault(), onClose());
  };
  return (
    <div className="quick-add" ref={box} style={{ left: pos.x, top: pos.y }} role="dialog" aria-label="Quick add">
      <span className="well search">
        ⌕
        <input
          autoFocus
          placeholder="Add a block…"
          value={query}
          onChange={(e) => (setQuery(e.target.value), setI(0))}
          onKeyDown={onKey}
          onKeyUp={(e) => e.stopPropagation()}
        />
      </span>
      <ol role="listbox">
        {list.map((it, k) => {
          const c = card(it);
          return (
            <li
              key={c.type}
              role="option"
              aria-selected={k === i}
              className={k === i ? "on" : undefined}
              onMouseEnter={() => setI(k)}
              onMouseDown={(e) => (e.preventDefault(), onPick(it))}
            >
              <b>{c.label}</b>
              <span className="lbl">
                {c.kind}
                {c.group ? ` · ${c.group}` : ""}
              </span>
            </li>
          );
        })}
        {list.length === 0 && <li className="none lbl">Nothing matches{flow ? ` in this ${flowLabel(flow)}` : ""}</li>}
        {flow &&
          away.map((it) => (
            <li key={`away-${it.def.type}`} className="none away" aria-disabled title={it.kind === "block" ? notHere(it.def, flow) : undefined}>
              <b>{card(it).label}</b>
              <span className="lbl">{it.kind === "block" ? `${flowsLabel(it.def.flows ?? [])} only` : ""}</span>
            </li>
          ))}
      </ol>
      <div className="quick-foot lbl">
        <span>↑↓ choose · ⏎ add here</span>
        <span>esc</span>
      </div>
    </div>
  );
}
