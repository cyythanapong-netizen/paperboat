import { useContext, useState } from "react";
import { NumberField } from "./NumberField";
import type { Variable } from "./protocol";
import { VarsContext } from "./Variables";

/** One variable as a control (#75): a slider (with its number) for a number with a range, a toggle for a boolean, a choice for an enum. A formula's is read-only. */
function VarControl({ v }: { v: Variable }) {
  const vars = useContext(VarsContext);
  const [draft, setDraft] = useState<number | null>(null); // a slider mid-drag: one change on release, not one per step
  if (!vars) return null;
  const now = vars.values[v.name] ?? v.value;
  const off = (vars.locked && !v.tunable) || v.expr !== undefined; // a run keeps its variables, but a tunable one (#75)
  const set = (value: Variable["value"]) => vars.setList(vars.list.map((x) => (x.name === v.name ? { ...x, value, expr: undefined } : x)));
  const unit = v.unit ? <span className="grey small">{v.unit}</span> : null;
  let control;
  if (v.type === "number") {
    const x = Number(now ?? 0);
    const ranged = v.min !== undefined && v.max !== undefined;
    const release = () => {
      if (draft !== null && draft !== v.value) set(draft);
      setDraft(null);
    };
    control = (
      <>
        {ranged && (
          <input
            type="range"
            aria-label={v.name}
            min={v.min}
            max={v.max}
            step={v.step ?? (v.whole ? 1 : (v.max! - v.min!) / 100)}
            disabled={off}
            value={draft ?? x}
            onChange={(e) => setDraft(Number(e.target.value))}
            onPointerUp={release}
            onKeyUp={release}
            onBlur={release}
          />
        )}
        {off ? (
          <output className="var-value out">{+(draft ?? x).toPrecision(6)}</output>
        ) : (
          <NumberField
            className="var-value well"
            aria-label={`${v.name} value`}
            value={draft ?? x}
            valid={(n) => (v.min === undefined || n >= v.min) && (v.max === undefined || n <= v.max) && (!v.whole || Number.isInteger(n))}
            onChange={set}
          />
        )}
        {unit}
      </>
    );
  } else if (v.type === "boolean") {
    control = <input type="checkbox" className="var-check" aria-label={v.name} disabled={off} checked={now === true} onChange={(e) => set(e.target.checked)} />;
  } else if (v.type === "enum") {
    control = (
      <select className="var-value well" aria-label={v.name} disabled={off} value={String(now)} onChange={(e) => set(e.target.value)}>
        {(v.options ?? []).map((o) => (
          <option key={o}>{o}</option>
        ))}
      </select>
    );
  } else {
    control = <input className="var-value well text" aria-label={v.name} disabled={off} value={String(now ?? "")} onChange={(e) => set(e.target.value)} />;
  }
  return (
    <label className="var-control" title={v.note}>
      <span className="var-control-name">{v.name}</span>
      {control}
    </label>
  );
}

/** The controls of the variables that `flag` marks, grouped by their `group`; nothing when none is marked. */
export function VarControls({ flag, empty }: { flag: "exposed" | "dock"; empty?: string }) {
  const vars = useContext(VarsContext);
  const marked = (vars?.list ?? []).filter((v) => v[flag]);
  if (marked.length === 0) return empty ? <span className="grey small">{empty}</span> : null;
  const groups = [...new Set(marked.map((v) => v.group ?? ""))];
  return (
    <>
      {groups.map((g) => (
        <div key={g} className="var-controls-group">
          {g && <span className="lbl">{g}</span>}
          {marked.filter((v) => (v.group ?? "") === g).map((v) => (
            <VarControl key={v.name} v={v} />
          ))}
        </div>
      ))}
    </>
  );
}
