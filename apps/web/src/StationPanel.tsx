import { useState } from "react";
import { ParamInput } from "./FlowEditor";
import type { ObjectDef, Station, StationObject } from "./protocol";
import { withoutObject } from "./StationGraph";

type Props = {
  objects: ObjectDef[];
  station: Station;
  /** Taken ids (station objects and flow blocks): a new object's linked block shares its id. */
  taken: Set<string>;
  onChange: (station: Station) => void;
  /** No edits while a run uses the station. */
  locked: boolean;
};

function freeId(type: string, taken: Set<string>): string {
  let i = 1;
  while (taken.has(`${type}-${i}`)) i++;
  return `${type}-${i}`;
}

/**
 * The Station page's setup: add and remove station objects and set their properties
 * (initial conditions included). Properties live here only, never on the Flow page.
 */
export function StationPanel({ objects, station, taken, onChange, locked }: Props) {
  const [open, setOpen] = useState(true);
  const defs = new Map(objects.map((o) => [o.type, o]));

  const update = (id: string, change: (o: StationObject) => StationObject) =>
    onChange({ objects: station.objects.map((o) => (o.id === id ? change(o) : o)) });
  const add = (def: ObjectDef) => {
    const props = Object.fromEntries(def.props.map((p) => [p.id, p.default]));
    const pos: [number, number] = [220 * station.objects.length, 320]; // where it appears in a graph view
    onChange({ objects: [...station.objects, { id: freeId(def.type, taken), type: def.type, props, pos }] });
  };
  // An object goes with every link to it (a spring loses the end it was holding).
  const remove = (id: string) => onChange(withoutObject(station, id));

  return (
    <div className="station-panel">
      <button className="station-head" onClick={() => setOpen(!open)}>
        {open ? "▾" : "▸"} Station · {station.objects.length} object{station.objects.length === 1 ? "" : "s"}
        {locked && <span className="muted"> · locked while running</span>}
      </button>
      {open && (
        <>
          <div className="station-add">
            {objects.map((d) => (
              <button key={d.type} disabled={locked} onClick={() => add(d)} title={d.block ? `Also adds a linked ${d.block} block to the flow` : undefined}>
                + {d.label}
              </button>
            ))}
          </div>
          {station.objects.map((o) => {
            const def = defs.get(o.type);
            return (
              <div key={o.id} className="station-object">
                <div className="block-title">
                  <span>
                    {def?.label ?? o.type} <span className="linked-tag">{o.id}</span>
                  </span>
                  <button className="remove" disabled={locked} title="Remove (and its linked block)" onClick={() => remove(o.id)}>
                    ×
                  </button>
                </div>
                {Object.entries(o.links ?? {}).map(([slot, target]) => (
                  <div key={slot} className="param muted">
                    {slot} <span>{target}</span>
                  </div>
                ))}
                {def?.props.map((p) => (
                  <label key={p.id} className="param">
                    {p.id}
                    <ParamInput
                      def={p}
                      value={o.props?.[p.id] ?? p.default}
                      disabled={locked}
                      set={(v) => update(o.id, (x) => ({ ...x, props: { ...x.props, [p.id]: v } }))}
                    />
                  </label>
                ))}
              </div>
            );
          })}
        </>
      )}
    </div>
  );
}
