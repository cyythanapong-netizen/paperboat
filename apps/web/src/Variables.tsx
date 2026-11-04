import { createContext, useContext, useEffect, useMemo, useRef, useState, type KeyboardEvent, type ReactNode } from "react";
import { parseNumber } from "./NumberField";
import type { ParamDef, ParamValue, Variable, Variables } from "./protocol";
import { badName, emptyOf, fits, freeName, isRef, type Ref, type Use } from "./bindings";

/**
 * Variables (#36): named, typed values used by reference in block params and station
 * properties. The list lives in the App (the backend's defaults, then the user's own); the
 * engine resolves it and says what each comes to (`values`), or why it doesn't hold.
 */
export type VarsApi = {
  list: Variables;
  values: Record<string, number | boolean | string>;
  error: { text: string; at?: string } | null;
  /** Names of the backend's defaults (Reset instead of Delete). */
  defaults: Variables;
  locked: boolean;
  setList: (next: Variables) => void;
  rename: (from: string, to: string) => void;
  remove: (name: string) => void;
  uses: (name: string) => Use[];
  /** Open the sheet at `name`. */
  reveal: (name: string) => void;
  /** A new variable from a field's value; returns its name. */
  promote: (def: ParamDef, value: ParamValue) => string;
};
export const VarsContext = createContext<VarsApi | null>(null);

/** A variable being dragged from the sheet onto a field (dataTransfer can't be read on dragover). */
const dragged = { current: null as Variable | null };
const VAR_DRAG = "application/x-paperboat-variable";

const show = (v: unknown) => (typeof v === "number" ? String(+v.toPrecision(6)) : typeof v === "boolean" ? (v ? "on" : "off") : String(v ?? "–"));

// ---------------------------------------------------------------------------------------
// Fields: a bound field's chip, "=" in a number field, and dropping a variable on a field.

/**
 * Wraps a param or property field: a variable dragged from the sheet can be dropped on it
 * when its type fits; a bound value shows as a chip instead of the field.
 */
export function Bindable({ def, value, set, disabled, children }: { def: ParamDef; value: ParamValue; set: (v: ParamValue) => void; disabled?: boolean; children: ReactNode }) {
  const vars = useContext(VarsContext);
  const [over, setOver] = useState(false);
  if (!vars) return <>{children}</>;
  const ok = () => !disabled && !!dragged.current && fits(dragged.current, def);
  return (
    <span
      className={`bindable${over ? " drop-on" : ""}`}
      onDragOver={(e) => {
        if (!e.dataTransfer.types.includes(VAR_DRAG) || !ok()) return;
        e.preventDefault();
        e.dataTransfer.dropEffect = "link";
        setOver(true);
      }}
      onDragLeave={() => setOver(false)}
      onDrop={(e) => {
        setOver(false);
        if (!ok()) return;
        e.preventDefault();
        set({ var: dragged.current!.name });
      }}
    >
      {isRef(value) ? <Bound def={def} value={value} set={set} disabled={disabled} /> : children}
    </span>
  );
}

/** A bound field: the variable (or formula) in an ink outline, what it comes to after it. Click: edit in place. */
function Bound({ def, value, set, disabled }: { def: ParamDef; value: Ref; set: (v: ParamValue) => void; disabled?: boolean }) {
  const vars = useContext(VarsContext)!;
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    if (!open) return;
    const away = (e: MouseEvent) => !box.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener("mousedown", away);
    return () => window.removeEventListener("mousedown", away);
  }, [open]);
  const isVar = "var" in value;
  const v = isVar ? vars.list.find((x) => x.name === value.var) : undefined;
  const missing = isVar && !v;
  const now = isVar ? vars.values[value.var] : undefined;
  const fallback = def.default as ParamValue;
  const keep = now !== undefined ? now : fallback;
  return (
    <span className="bound-box" ref={box} onKeyDown={(e) => e.key === "Escape" && setOpen(false)}>
      <button
        type="button"
        className={`bound nodrag${missing ? " bad" : ""}${open ? " hot" : ""}`}
        disabled={disabled}
        title={isVar ? (missing ? `No variable called ${value.var}` : `The variable ${value.var}: click to edit it here`) : `A formula: ${value.expr}`}
        onClick={() => setOpen(!open)}
      >
        <span className={`ref${isVar ? "" : " f"}`}>{isVar ? value.var : value.expr}</span>
        {isVar && <span className="val">{missing ? "?" : show(now ?? v?.value)}</span>}
      </button>
      {open && (
        <div className="pop bound-pop nodrag nopan nowheel" role="dialog" aria-label="Bound field">
          <div className="card">
            {isVar && v && (
              <>
                <div className="row">
                  <code>{v.name}</code>
                  <span className="grey">=</span>
                  {v.expr !== undefined && v.type !== "number" ? <output>{show(now)}</output> : <ValueField v={v} onChange={(x) => vars.setList(vars.list.map((y) => (y.name === v.name ? x : y)))} disabled={vars.locked} />}
                  {v.unit && <span className="grey mono">{v.unit}</span>}
                </div>
                <span className="grey small">
                  {v.expr !== undefined ? `A formula: ${v.expr}. ` : ""}
                  {count(vars.uses(v.name).length, "use")}: changing it changes every one.
                </span>
              </>
            )}
            {missing && <span className="bad-text small">No variable called {(value as { var: string }).var}: the run won't start.</span>}
            {!isVar && <FormulaEdit value={value.expr} onSet={(expr) => set(expr ? { expr } : fallback)} />}
            <div className="btns">
              {isVar && v && (
                <button type="button" className="key small" onClick={() => (setOpen(false), vars.reveal(v.name))}>
                  Show in Variables
                </button>
              )}
              <button type="button" className="key small" onClick={() => (setOpen(false), set(keep))}>
                Unbind (keep {show(keep)})
              </button>
            </div>
          </div>
        </div>
      )}
    </span>
  );
}

function FormulaEdit({ value, onSet }: { value: string; onSet: (expr: string) => void }) {
  const [text, setText] = useState(value);
  return (
    <label className="row">
      <span className="grey">ƒ</span>
      <input className="formula-input" value={text} spellCheck={false} onChange={(e) => setText(e.target.value)} onBlur={() => onSet(text.trim())} onKeyDown={(e) => e.key === "Enter" && onSet(text.trim())} />
    </label>
  );
}

/**
 * A number param's field that also binds: typing "=" lists the number variables that fit
 * (Enter binds one), takes a formula ("=2 * kp_x"), or makes a variable of this value.
 */
export function NumberOrFormula({ def, value, set, disabled }: { def: ParamDef; value: number; set: (v: ParamValue) => void; disabled?: boolean }) {
  const vars = useContext(VarsContext);
  const [text, setText] = useState<string | null>(null);
  const [pick, setPick] = useState(0);
  const formula = text !== null && text.startsWith("=") && !!vars;
  const q = formula ? text!.slice(1).trim() : "";
  const fragment = /^\w*$/.test(q);
  const choices = useMemo(() => {
    if (!formula || !fragment || !vars) return [];
    const fitting = vars.list.filter((v) => fits(v, def));
    const named = fitting.filter((v) => v.name.toLowerCase().includes(q.toLowerCase()));
    return named.sort((a, b) => Number(!a.name.startsWith(q)) - Number(!b.name.startsWith(q))).slice(0, 6);
  }, [formula, fragment, vars, def, q]);
  const fresh = vars && formula ? freeName(fragment && q ? q : def.id, vars.list) : "";
  const items = formula ? [...choices.map((v) => ({ bind: v.name })), { fresh }] : [];
  const done = () => setText(null);
  const choose = (i: number) => {
    const it = items[i];
    if (!it) return;
    if ("bind" in it) set({ var: it.bind });
    else if (vars) set({ var: vars.promote({ ...def, id: it.fresh }, value) });
    done();
  };
  const onKey = (e: KeyboardEvent<HTMLInputElement>) => {
    if (!formula) return;
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      setPick((p) => (p + (e.key === "ArrowDown" ? 1 : items.length - 1)) % Math.max(1, items.length));
    } else if (e.key === "Enter") {
      e.preventDefault();
      e.stopPropagation();
      if (!fragment) set({ expr: q });           // a formula
      else if (q && vars!.list.some((v) => v.name === q && fits(v, def))) set({ var: q });
      else return choose(pick);
      done();
    } else if (e.key === "Escape") {
      e.preventDefault();
      done();
    }
  };
  return (
    <span className="formula-box">
      <input
        className={`nodrag num${formula ? " formula" : ""}`}
        type="text"
        inputMode="decimal"
        spellCheck={false}
        disabled={disabled}
        value={text ?? String(value)}
        onChange={(e) => {
          setText(e.target.value);
          setPick(0);
          if (!e.target.value.startsWith("=")) {
            const v = parseNumber(e.target.value);
            if (v !== undefined && v !== value) set(v);
          }
        }}
        onBlur={() => setTimeout(done, 120)}
        onKeyDown={onKey}
        title={vars ? "A number, or = for a variable or a formula" : undefined}
      />
      {formula && (
        <div className="pop formula-pop nodrag nopan nowheel" role="listbox">
          {fragment ? (
            <>
              <div className="head">{choices.length ? "Variables that fit" : "No variable fits yet"}</div>
              {items.map((it, i) => (
                <div
                  key={"bind" in it ? it.bind : "new"}
                  role="option"
                  aria-selected={i === pick}
                  className={`item${i === pick ? " on" : ""}`}
                  onMouseDown={(e) => (e.preventDefault(), choose(i))}
                >
                  {"bind" in it ? (
                    <>
                      <code>{it.bind}</code>
                      <span className="v">
                        {show(vars!.values[it.bind] ?? vars!.list.find((v) => v.name === it.bind)?.value)} {vars!.list.find((v) => v.name === it.bind)?.unit ?? ""}
                      </span>
                    </>
                  ) : (
                    <>
                      <span>
                        + New variable <code>{it.fresh}</code>
                      </span>
                      <span className="v">= {show(value)}</span>
                    </>
                  )}
                </div>
              ))}
            </>
          ) : (
            <div className="head">A formula: Enter sets it</div>
          )}
          <div className="foot">
            = starts a formula: <code>1.25 * kp_x</code>, <code>sqrt(g / len)</code>. Esc keeps {show(value)}
          </div>
        </div>
      )}
    </span>
  );
}

/** A variable's value, edited as its type. */
function ValueField({ v, onChange, disabled, wide }: { v: Variable; onChange: (v: Variable) => void; disabled?: boolean; wide?: boolean }) {
  const set = (value: Variable["value"]) => onChange({ ...v, value });
  if (v.type === "number") return <NumberCell v={v} onChange={onChange} disabled={disabled} wide={wide} />;
  if (v.type === "boolean")
    return <input type="checkbox" className="var-check" disabled={disabled} checked={v.value === true} onChange={(e) => set(e.target.checked)} />;
  if (v.type === "enum")
    return (
      <select className="var-value well" disabled={disabled} value={String(v.value)} onChange={(e) => set(e.target.value)}>
        {(v.options ?? []).map((o) => (
          <option key={o}>{o}</option>
        ))}
      </select>
    );
  return <input className="var-value well text" disabled={disabled} value={String(v.value ?? "")} onChange={(e) => set(e.target.value)} />;
}

/**
 * A number variable's one field, like a spreadsheet cell: a number, or a formula over other
 * variables ("kp_theta / 5"). It shows what it comes to; while editing, what was typed. A
 * number applies as you type; a formula on Enter or leaving the field (half-typed, it
 * isn't one yet). Stored as `value` when it's a plain number, else `expr`: a plain number
 * is what a knob or a --var can set, a formula is derived.
 */
function NumberCell({ v, onChange, disabled, wide }: { v: Variable; onChange: (v: Variable) => void; disabled?: boolean; wide?: boolean }) {
  const vars = useContext(VarsContext);
  const [text, setText] = useState<string | null>(null);
  const formula = v.expr !== undefined;
  const now = formula ? vars?.values[v.name] : v.value;
  const inRange = (x: number) => (v.min === undefined || x >= v.min) && (v.max === undefined || x <= v.max) && (!v.whole || Number.isInteger(x));
  const asNumber = (t: string) => parseNumber(t, inRange);
  const commit = () => {
    if (text === null) return;
    const t = text.trim();
    const x = asNumber(t);
    if (x !== undefined) onChange({ ...v, expr: undefined, value: x });
    else if (t && !/^[-+.\d\se]*$/.test(t)) onChange({ ...v, value: undefined, expr: t }); // not a half-typed number
    setText(null);
  };
  return (
    <input
      className={`var-value well${formula ? " is-formula" : ""}${wide ? " wide" : ""}`}
      aria-label={`${v.name}: a number or a formula`}
      title={formula ? `= ${v.expr}` : "A number, or a formula over other variables (kp_theta / 5)"}
      disabled={disabled}
      spellCheck={false}
      value={text ?? (formula && !wide ? show(now) : formula ? v.expr! : String(v.value ?? 0))}
      onFocus={() => setText(formula ? v.expr! : String(v.value ?? 0))}
      onChange={(e) => {
        setText(e.target.value);
        const x = asNumber(e.target.value);
        if (x !== undefined && (formula || x !== v.value)) onChange({ ...v, expr: undefined, value: x });
      }}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === "Enter") commit();
        else if (e.key === "Escape") setText(null);
      }}
    />
  );
}

/** An optional number (a range end, a step): empty means none. */
function OptionalNumber({ value, onChange, label }: { value?: number; onChange: (v?: number) => void; label: string }) {
  const [text, setText] = useState<string | null>(null);
  return (
    <input
      className="well opt-num"
      aria-label={label}
      placeholder="–"
      inputMode="decimal"
      value={text ?? (value === undefined ? "" : String(value))}
      onChange={(e) => {
        setText(e.target.value);
        if (e.target.value.trim() === "") onChange(undefined);
        else {
          const v = parseNumber(e.target.value);
          if (v !== undefined) onChange(v);
        }
      }}
      onBlur={() => setText(null)}
    />
  );
}

const count = (n: number, one: string) => `${n} ${one}${n === 1 ? "" : "s"}`;
const TYPES: Variable["type"][] = ["number", "boolean", "enum", "text", "file"];

// ---------------------------------------------------------------------------------------
// The sheet: every variable, beside whatever page is open.

export function VariablesSheet({
  open,
  onOpen,
  onClose,
  onShow,
  onUndo,
  toml,
}: {
  open: string | null;
  onOpen: (name: string | null) => void;
  onClose: () => void;
  onShow: (use: Use) => void;
  /** Ctrl+Z (false) / Ctrl+Shift+Z (true) in the sheet, outside a field. */
  onUndo: (redo: boolean) => void;
  /** The list as a descriptor's [[variables]]. */
  toml: () => string;
}) {
  const vars = useContext(VarsContext)!;
  const [search, setSearch] = useState("");
  const [copied, setCopied] = useState(false);
  const copy = () =>
    navigator.clipboard?.writeText(toml()).then(
      () => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1500);
      },
      () => undefined,
    );
  const root = useRef<HTMLElement>(null);
  // Bring the open one into view (Show in Variables, a formula's use).
  useEffect(() => {
    if (!open) return;
    root.current?.querySelector(`[data-var="${CSS.escape(open)}"]`)?.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }, [open]);
  const shown = vars.list.filter((v) => {
    const s = search.trim().toLowerCase();
    return !s || [v.name, v.unit, v.note, v.group].some((x) => x?.toLowerCase().includes(s));
  });
  const groups = [...new Set(shown.map((v) => v.group ?? ""))];
  const errorAt = vars.error?.at;
  const badVar = errorAt && vars.list.some((v) => v.name === errorAt) ? errorAt : null;
  const update = (name: string, next: Variable) => vars.setList(vars.list.map((v) => (v.name === name ? next : v)));
  const add = () => {
    const name = freeName("var", vars.list);
    vars.setList([...vars.list, { name, type: "number", value: 0 }]);
    onOpen(name);
  };
  return (
    <section
      className="panel vars-sheet"
      aria-label="Variables"
      ref={root}
      onKeyDown={(e) => {
        const inField = e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement;
        if (e.key === "Escape" && !inField) {
          e.preventDefault();
          onClose();
        } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z" && !inField) {
          e.preventDefault();
          e.stopPropagation(); // not the Flow's undo
          onUndo(e.shiftKey);
        }
      }}
    >
      <header className="panel-head">
        <span className="num mono">=</span>
        <h2>Variables</h2>
        <span className="panel-info lbl">
          {vars.list.length}
          {vars.error ? " · 1 error" : ""}
        </span>
        <button className="key small" disabled={vars.locked} onClick={add} title="A new number variable">
          + New
        </button>
        <button className="key small x" onClick={onClose} title="Close (Alt+V, Esc)" aria-label="Close the Variables">
          ✕
        </button>
      </header>
      <div className="vars-tools">
        <span className="well">
          <input aria-label="Search variables" placeholder="Search names, units, notes…" value={search} onChange={(e) => setSearch(e.target.value)} />
        </span>
      </div>
      {vars.error && !badVar && <p className="vars-error">{vars.error.text}</p>}
      <div className="vars-list" role="list">
        {vars.list.length === 0 && (
          <p className="empty">
            No variables yet. <b>+ New</b> makes one; or type <code>=</code> in a number field to make one from its value. A variable is a value used in many places: change it once, and every block and property that uses it follows.
          </p>
        )}
        {groups.map((g) => (
          <div key={g} role="group" aria-label={g || "Variables"}>
            {g && <div className="vars-group">{g}</div>}
            {shown
              .filter((v) => (v.group ?? "") === g)
              .map((v) => (
                <Row
                  key={v.name}
                  v={v}
                  open={open === v.name}
                  bad={badVar === v.name ? vars.error!.text : ""}
                  onOpen={() => onOpen(open === v.name ? null : v.name)}
                  onChange={(next) => update(v.name, next)}
                  onShow={onShow}
                />
              ))}
          </div>
        ))}
      </div>
      <footer className="vars-foot">
        <span>{vars.locked ? "Locked while running" : "Changes apply to the next run"}</span>
        <button className="link" onClick={copy} disabled={vars.list.length === 0} title="Copy them as a descriptor's [[variables]]: paste there to make them the backend's defaults">
          {copied ? "Copied" : "Copy as TOML"}
        </button>
      </footer>
    </section>
  );
}

function Row({ v, open, bad, onOpen, onChange, onShow }: { v: Variable; open: boolean; bad: string; onOpen: () => void; onChange: (v: Variable) => void; onShow: (use: Use) => void }) {
  const vars = useContext(VarsContext)!;
  const uses = vars.uses(v.name);
  const formula = v.expr !== undefined;
  return (
    <div className={`var-row${open ? " open" : ""}${bad ? " bad" : ""}`} data-var={v.name} role="listitem">
      <span
        className="var-name"
        draggable={!vars.locked}
        tabIndex={0}
        title="Click to open; drag onto a field to bind it"
        onClick={onOpen}
        onKeyDown={(e) => (e.key === "Enter" || e.key === " ") && (e.preventDefault(), onOpen())}
        onDragStart={(e) => {
          dragged.current = v;
          e.dataTransfer.setData(VAR_DRAG, v.name);
          e.dataTransfer.setData("text/plain", v.name);
          e.dataTransfer.effectAllowed = "link";
        }}
        onDragEnd={() => (dragged.current = null)}
      >
        {v.name}
      </span>
      <span className="var-type">{formula && v.type !== "number" ? "ƒ formula" : v.type}</span>
      {formula && v.type !== "number" ? <output className="var-value out">{show(vars.values[v.name])}</output> : <ValueField v={v} onChange={onChange} disabled={vars.locked} />}
      <span className="var-unit">{v.unit ?? ""}</span>
      <button className={`var-uses${uses.length ? "" : " none"}`} onClick={onOpen} tabIndex={-1}>
        {uses.length ? count(uses.length, "use") : "unused"}
      </button>
      {formula && <div className="var-expr">= {v.expr}</div>}
      {bad && <div className="var-expr bad-text">{bad}</div>}
      {open && <Detail v={v} uses={uses} onChange={onChange} onShow={onShow} />}
    </div>
  );
}

function Detail({ v, uses, onChange, onShow }: { v: Variable; uses: Use[]; onChange: (v: Variable) => void; onShow: (use: Use) => void }) {
  const vars = useContext(VarsContext)!;
  const [name, setName] = useState(v.name);
  const [confirm, setConfirm] = useState(false);
  useEffect(() => setName(v.name), [v.name]);
  const why = badName(name, vars.list, v.name);
  const def = vars.defaults.find((d) => d.name === v.name);
  const changed = def && JSON.stringify(def) !== JSON.stringify(v);
  const formula = v.expr !== undefined;
  const can = (t: Variable["type"]) => t === "boolean"; // a number's one field takes a formula too
  const rename = () => {
    if (name !== v.name && !why) vars.rename(v.name, name);
    else setName(v.name);
  };
  return (
    <div className="var-detail" onClick={(e) => e.stopPropagation()}>
      <span className="lbl">Name</span>
      <span className="wells">
        <input className="well wide mono" aria-label="Name" value={name} disabled={vars.locked || !!def} title={def ? "The backend's default: its name is fixed" : "Renames every use"} onChange={(e) => setName(e.target.value)} onBlur={rename} onKeyDown={(e) => e.key === "Enter" && rename()} />
      </span>
      {why && name !== v.name && <span className="bad-text small span2">{why}</span>}
      <span className="lbl">Type</span>
      <span className="opts" role="radiogroup" aria-label="Type">
        {TYPES.map((t) => (
          <button
            key={t}
            role="radio"
            aria-checked={v.type === t}
            className={`opt${v.type === t ? " on" : ""}`}
            disabled={vars.locked}
            onClick={() =>
              v.type !== t &&
              onChange({ name: v.name, type: t, value: emptyOf(t), ...(t === "enum" ? { options: ["a", "b"] } : {}), ...(v.group ? { group: v.group } : {}), ...(v.note ? { note: v.note } : {}) })
            }
          >
            {t}
          </button>
        ))}
      </span>
      {can(v.type) && (
        <>
          <span className="lbl">Holds</span>
          <span className="opts" role="radiogroup" aria-label="Holds">
            <button role="radio" aria-checked={!formula} className={`opt${!formula ? " on" : ""}`} disabled={vars.locked} onClick={() => formula && onChange({ ...v, expr: undefined, value: vars.values[v.name] ?? emptyOf(v.type) })}>
              a value
            </button>
            <button role="radio" aria-checked={formula} className={`opt${formula ? " on" : ""}`} disabled={vars.locked} onClick={() => !formula && onChange({ ...v, value: undefined, expr: String(v.value ?? 0) })}>
              a formula
            </button>
          </span>
        </>
      )}
      {v.type === "number" && (
        <>
          <span className="lbl">Value</span>
          <span className="wells">
            <ValueField v={v} onChange={onChange} disabled={vars.locked} wide />
          </span>
        </>
      )}
      {formula && v.type === "boolean" && (
        <>
          <span className="lbl">Formula</span>
          <span className="wells">
            <FormulaInput value={v.expr!} onSet={(expr) => onChange({ ...v, expr })} />
          </span>
        </>
      )}
      {v.type === "enum" && (
        <>
          <span className="lbl">Options</span>
          <span className="wells">
            <OptionsInput value={v.options ?? []} onSet={(options) => onChange({ ...v, options, value: options.includes(String(v.value)) ? v.value : options[0] })} />
          </span>
        </>
      )}
      {v.type === "number" && (
        <>
          <span className="lbl">Unit</span>
          <span className="wells">
            <input className="well mono unit" aria-label="Unit" value={v.unit ?? ""} placeholder="m, N, rad…" disabled={vars.locked} onChange={(e) => onChange({ ...v, unit: e.target.value || undefined })} />
          </span>
          <span className="lbl">Range</span>
          <span className="wells">
            <OptionalNumber label="Minimum" value={v.min} onChange={(min) => onChange({ ...v, min })} /> to <OptionalNumber label="Maximum" value={v.max} onChange={(max) => onChange({ ...v, max })} /> step{" "}
            <OptionalNumber label="Step" value={v.step} onChange={(step) => onChange({ ...v, step: step && step > 0 ? step : undefined })} />
            <label className="check">
              <input type="checkbox" checked={!!v.whole} onChange={(e) => onChange({ ...v, whole: e.target.checked || undefined })} /> whole
            </label>
          </span>
        </>
      )}
      <span className="lbl">Note</span>
      <span className="wells">
        <input className="well wide" aria-label="Note" value={v.note ?? ""} placeholder="What it's for" onChange={(e) => onChange({ ...v, note: e.target.value || undefined })} />
      </span>
      <span className="lbl">Group</span>
      <span className="wells">
        <input className="well wide" aria-label="Group" value={v.group ?? ""} placeholder="Plant, Controller…" onChange={(e) => onChange({ ...v, group: e.target.value || undefined })} />
      </span>
      <span className="lbl">Used by</span>
      <span className="grey small">{uses.length ? "" : "Nothing yet: drag the name onto a field, or type = in one"}</span>
      {uses.length > 0 && (
        <div className="var-uses-list">
          {uses.map((u) => (
            <div key={`${u.page}:${u.id}:${u.field}`} className="var-use">
              <code>{u.field ? `${u.id} · ${u.field}` : u.id}</code>
              <span className="where">{u.page === "variables" ? "in its formula" : u.formula ? "a formula" : u.page === "flow" ? "Flow" : "Station"}</span>
              <button className="key small" onClick={() => onShow(u)}>
                Show
              </button>
            </div>
          ))}
        </div>
      )}
      <div className="var-actions">
        {confirm ? (
          <>
            <span className="small">
              Delete {v.name}?{uses.length ? ` Its ${count(uses.length, "use")} keep its value, ${show(vars.values[v.name] ?? v.value)}.` : ""}
            </span>
            <button className="key small" onClick={() => vars.remove(v.name)}>
              Delete
            </button>
            <button className="key small" onClick={() => setConfirm(false)}>
              Keep
            </button>
          </>
        ) : def ? (
          <button className="key small" disabled={!changed || vars.locked} title="The backend's default: it can be reset, not deleted" onClick={() => onChange(def)}>
            Reset to default
          </button>
        ) : (
          <button className="key small" disabled={vars.locked} onClick={() => setConfirm(true)}>
            Delete…
          </button>
        )}
      </div>
    </div>
  );
}

function FormulaInput({ value, onSet }: { value: string; onSet: (v: string) => void }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  const commit = () => text.trim() && text.trim() !== value && onSet(text.trim());
  return <input className="well wide mono" aria-label="Formula" spellCheck={false} value={text} onChange={(e) => setText(e.target.value)} onBlur={commit} onKeyDown={(e) => e.key === "Enter" && commit()} />;
}

function OptionsInput({ value, onSet }: { value: string[]; onSet: (v: string[]) => void }) {
  const [text, setText] = useState(value.join(", "));
  useEffect(() => setText(value.join(", ")), [value]);
  const commit = () => {
    const list = [...new Set(text.split(",").map((s) => s.trim()).filter(Boolean))];
    if (list.length) onSet(list);
  };
  return <input className="well wide mono" aria-label="Options" value={text} placeholder="pid, lqr" onChange={(e) => setText(e.target.value)} onBlur={commit} onKeyDown={(e) => e.key === "Enter" && commit()} />;
}
