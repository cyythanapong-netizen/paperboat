import { useContext, useId } from "react";
import { Bindable, draggedVariable, NumberOrFormula, VAR_DRAG, VarsContext } from "./Variables";
import { isRef } from "./bindings";
import type { ParamDef, ParamValue } from "./protocol";

/**
 * A param's (or a station property's) field. It can be bound to a variable (#36): drop one
 * on it from the Variables sheet, or type = in a number field; a bound one shows as a chip.
 */
export function ParamInput(props: { def: ParamDef; value: ParamValue; set: (v: ParamValue) => void; disabled?: boolean }) {
  return (
    <Bindable def={props.def} value={props.value} set={props.set} disabled={props.disabled}>
      {isRef(props.value) ? null : <Field {...props} />}
    </Bindable>
  );
}

/** A param (or station property) as a row: its id and unit, then its field. */
export function ParamRow(props: Parameters<typeof ParamInput>[0]) {
  const { def } = props;
  return (
    <label className="param">
      <span>
        {def.id}
        {"unit" in def && def.unit && <span className="unit"> {def.unit}</span>}
      </span>
      <ParamInput {...props} />
    </label>
  );
}

/** A number field, or for a `key` param a field that takes the next key you press. */
function Field({
  def,
  value,
  set,
  disabled,
}: {
  def: ParamDef;
  value: ParamValue;
  set: (v: ParamValue) => void;
  disabled?: boolean;
}) {
  if (def.type === "boolean") {
    return (
      <input
        className="nodrag bool-param"
        type="checkbox"
        disabled={disabled}
        checked={value === true || value === 1}
        onChange={(e) => set(e.target.checked)}
      />
    );
  }
  if (def.type === "enum") {
    return (
      <select className="nodrag enum-param" disabled={disabled} value={String(value)} onChange={(e) => set(e.target.value)}>
        {def.options.map((o) => (
          <option key={o}>{o}</option>
        ))}
      </select>
    );
  }
  if (def.type === "file" || def.type === "text") {
    return (
      <input
        className="nodrag text-param"
        disabled={disabled}
        value={String(value)}
        title={def.type === "file" ? "A file, relative to the backend's descriptor" : undefined}
        onChange={(e) => set(e.target.value)}
      />
    );
  }
  if (def.type === "variable") return <VariableName value={String(value)} set={set} disabled={disabled} />;
  if (def.type === "key") {
    return (
      <input
        className="nodrag key-param"
        disabled={disabled}
        readOnly
        value={String(value)}
        title="Click, then press a key (Esc to keep)"
        onKeyDown={(e) => {
          e.preventDefault();
          if (e.code !== "Escape") set(e.code);
          e.currentTarget.blur();
        }}
      />
    );
  }
  return (
    <NumberOrFormula def={def} value={Number(value)} set={set} disabled={disabled} />
  );
}

/** A `variable` param: the name of a project variable, typed or picked from the list. A variable dragged from the sheet can be dropped on it. */
function VariableName({ value, set, disabled }: { value: string; set: (v: ParamValue) => void; disabled?: boolean }) {
  const vars = useContext(VarsContext);
  const list = useId();
  const unknown = !!value && !!vars && !vars.list.some((v) => v.name === value);
  return (
    <>
      <input
        className="nodrag text-param"
        list={list}
        disabled={disabled}
        value={value}
        title={unknown ? `No variable called "${value}"` : "A project variable"}
        aria-invalid={unknown}
        onChange={(e) => set(e.target.value)}
        onDragOver={(e) => e.dataTransfer.types.includes(VAR_DRAG) && !disabled && e.preventDefault()}
        onDrop={(e) => {
          const name = draggedVariable();
          if (!name || disabled) return;
          e.preventDefault();
          set(name);
        }}
      />
      <datalist id={list}>
        {vars?.list.map((v) => (
          <option key={v.name} value={v.name} />
        ))}
      </datalist>
    </>
  );
}
