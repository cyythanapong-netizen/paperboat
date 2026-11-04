import { useState } from "react";
import { ParamInput } from "./FlowEditor";
import { RemoveIcon } from "./RemoveIcon";
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

/** A new station object of `def`, with its defaults, and an id nothing else has. */
export function withObject(station: Station, def: ObjectDef, taken: Set<string>, pos?: [number, number]): Station {
  let i = 1;
  while (taken.has(`${def.type}-${i}`)) i++;
  const props = Object.fromEntries(def.props.map((p) => [p.id, p.default]));
  const at: [number, number] = pos ?? [220 * station.objects.length, 320]; // where it appears in a graph view
  return { objects: [...station.objects, { id: `${def.type}-${i}`, type: def.type, props, pos: at }] };
}

/**
 * The Station page's setup: the station objects, their properties (initial conditions
 * included) and removing them. Properties live here only, never on the Flow page. Objects
 * are added from the browser (the page's Add key, or dropped onto the station).
 */
export function StationPanel({ objects, station, onChange, locked }: Props) {
  const [open, setOpen] = useState(true);
  const defs = new Map(objects.map((o) => [o.type, o]));

  const update = (id: string, change: (o: StationObject) => StationObject) =>
    onChange({ objects: station.objects.map((o) => (o.id === id ? change(o) : o)) });
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
          {station.objects.map((o) => {
            const def = defs.get(o.type);
            return (
              <div key={o.id} className="station-object">
                <div className="block-title">
                  <span>
                    {def?.label ?? o.type} <span className="linked-tag">{o.id}</span>
                  </span>
                  <button className="remove" disabled={locked} title="Remove (and its linked block)" onClick={() => remove(o.id)}>
                    <RemoveIcon />
                  </button>
                </div>
                {Object.entries(o.links ?? {}).map(([slot, target]) => (
                  <div key={slot} className="param muted">
                    {slot} <span>{target}</span>
                  </div>
                ))}
                {def?.props.map((p) => (
                  <label key={p.id} className="param">
                    <span>
                      {p.id}
                      {"unit" in p && p.unit && <span className="unit"> {p.unit}</span>}
                    </span>
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
