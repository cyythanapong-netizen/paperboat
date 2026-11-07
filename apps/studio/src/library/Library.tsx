// The class tree (contracts §8.2): Modelica's packages opened one at a time, the model's own
// siblings, a filter box. Each part shows its imported icon. Drag a part onto the canvas, or
// double-click it, to place it.

import { Icon } from "@paperboat/shared/components";
import type { IconDef } from "@paperboat/shared";
import { useCallback, useEffect, useRef, useState } from "react";
import type { OmcClient } from "../omc/omc.ts";
import { atom, list, parseValue } from "../omc/parse.ts";
import { children, flat, search, type Entry } from "./classes.ts";

export const DRAG_TYPE = "application/x-squero-class";

type Props = { omc: OmcClient; model: string; onPlace: (cls: string) => void };

/** A part's icon, fetched only once its row scrolls into view: importing one costs omc a few calls. */
function Thumb({ omc, cls }: { omc: OmcClient; cls: string }) {
  const [icon, setIcon] = useState<IconDef>();
  const [seen, setSeen] = useState(false);
  const box = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    const el = box.current;
    if (!el || typeof IntersectionObserver === "undefined") return setSeen(true);
    const watch = new IntersectionObserver((e) => e.some((x) => x.isIntersecting) && (setSeen(true), watch.disconnect()));
    watch.observe(el);
    return () => watch.disconnect();
  }, []);
  useEffect(() => {
    if (!seen) return;
    let live = true;
    omc.op<{ icon?: IconDef }>("icon", { class: cls }).then((r) => live && setIcon(r.icon), () => undefined);
    return () => {
      live = false;
    };
  }, [omc, cls, seen]);
  // {param} text is for the placed part; a thumbnail leaves it out
  return (
    <span className="thumb" ref={box}>
      {icon ? <Icon icon={icon} type={cls} fill={(t) => (t.includes("{") ? "" : t)} /> : null}
    </span>
  );
}

function Row({ entry, omc, depth, onPlace }: { entry: Entry; omc: OmcClient; depth: number; onPlace: (cls: string) => void }) {
  return (
    <li
      className="lib-part"
      style={{ paddingLeft: 12 + depth * 14 }}
      draggable
      title={entry.comment ? `${entry.full}\n${entry.comment}` : entry.full}
      onDragStart={(e) => {
        e.dataTransfer.setData(DRAG_TYPE, entry.full);
        e.dataTransfer.effectAllowed = "copy";
      }}
      onDoubleClick={() => onPlace(entry.full)}
      onKeyDown={(e) => e.key === "Enter" && onPlace(entry.full)}
      tabIndex={0}
    >
      <Thumb omc={omc} cls={entry.full} />
      <span className="lib-name">{entry.name}</span>
      {entry.comment && <span className="lib-note">{entry.comment}</span>}
    </li>
  );
}

function Package({ pkg, name, omc, depth, onPlace, open: start = false }: { pkg: string; name: string; omc: OmcClient; depth: number; onPlace: (cls: string) => void; open?: boolean }) {
  const [open, setOpen] = useState(start);
  const [kids, setKids] = useState<Entry[] | null>(null);
  const [error, setError] = useState("");
  useEffect(() => {
    if (!open || kids) return;
    children(omc, pkg).then(setKids, (e: Error) => setError(e.message));
  }, [open, kids, omc, pkg]);
  return (
    <li className="lib-pkg">
      <button className="lib-head" style={{ paddingLeft: 12 + depth * 14 }} aria-expanded={open} onClick={() => setOpen(!open)}>
        <span className="lib-caret" aria-hidden="true">{open ? "−" : "+"}</span>
        {name}
      </button>
      {open && (
        <ul>
          {error && <li className="lib-note" style={{ paddingLeft: 26 + depth * 14 }}>{error}</li>}
          {!kids && !error && <li className="lib-note" style={{ paddingLeft: 26 + depth * 14 }}>reading…</li>}
          {kids?.map((k) => (k.kind === "package" ? <Package key={k.full} pkg={k.full} name={k.name} omc={omc} depth={depth + 1} onPlace={onPlace} /> : <Row key={k.full} entry={k} omc={omc} depth={depth + 1} onPlace={onPlace} />))}
        </ul>
      )}
    </li>
  );
}

/** Every part under Modelica, once, for the filter and quick-add. */
export function useClassIndex(omc: OmcClient): string[] {
  const [names, setNames] = useState<string[]>([]);
  const started = useRef(false);
  useEffect(() => {
    if (started.current) return;
    started.current = true;
    flat(omc, "Modelica").then(setNames, () => undefined);
  }, [omc]);
  return names;
}

export function Library({ omc, model, onPlace }: Props) {
  const [query, setQuery] = useState("");
  const [mine, setMine] = useState<string[]>([]);
  const index = useClassIndex(omc);
  const load = useCallback(async () => {
    const tops = list(parseValue(await omc.call("getClassNames()"))).map(atom);
    setMine(tops.filter((n) => n && !["Modelica", "ModelicaServices", "Complex", model].includes(n)));
  }, [omc, model]);
  useEffect(() => {
    void load().catch(() => undefined);
  }, [load]);
  const hits = search(index, query);
  return (
    <nav className="library" aria-label="Class library">
      <input className="lib-filter" type="search" placeholder="Filter parts…" value={query} onChange={(e) => setQuery(e.target.value)} aria-label="Filter parts" />
      <ul className="lib-tree">
        {query ? (
          hits.length ? (
            hits.map((n) => <Row key={n} entry={{ name: n.slice(n.lastIndexOf(".") + 1), full: n, kind: "part", comment: n.slice(0, n.lastIndexOf(".")) }} omc={omc} depth={0} onPlace={onPlace} />)
          ) : (
            <li className="lib-note">{index.length ? "nothing matches" : "reading the library…"}</li>
          )
        ) : (
          <>
            {mine.length > 0 && (
              <li className="lib-pkg">
                <div className="lib-head static">This project</div>
                <ul>{mine.map((n) => <Row key={n} entry={{ name: n, full: n, kind: "part", comment: "" }} omc={omc} depth={1} onPlace={onPlace} />)}</ul>
              </li>
            )}
            <Package pkg="Modelica" name="Modelica" omc={omc} depth={0} onPlace={onPlace} open />
          </>
        )}
      </ul>
    </nav>
  );
}
