// The split view (contracts §8.2): `[ .mo ] causal ODE c++` beside the canvas. .mo is the model's
// text from omc's `list()`, read-only this wave. C++ is the generated backend from a dry-run
// generate: its path and the reason above the code, nothing written to disk.

import { useEffect, useRef, useState } from "react";
import type { StudioModel } from "../model/types.ts";
import type { Actions } from "../store/index.tsx";

export type View = "mo" | "cpp";
const OPTIONS: { value: View; label: string }[] = [
  { value: "mo", label: ".mo" },
  { value: "cpp", label: "causal ODE c++" },
];

type Preview = Awaited<ReturnType<Actions["preview"]>>;

/** Runs `load` after the model settles; the text it returns, "…" while it runs, the words it failed with. */
function useLater<T>(model: StudioModel, load: () => Promise<T>, on: boolean): { value: T | null; error: string; busy: boolean } {
  const [state, setState] = useState<{ value: T | null; error: string; busy: boolean }>({ value: null, error: "", busy: false });
  const latest = useRef(load);
  latest.current = load;
  useEffect(() => {
    if (!on) return;
    let live = true;
    setState((s) => ({ ...s, busy: true }));
    const timer = window.setTimeout(() => {
      latest.current().then(
        (value) => live && setState({ value, error: "", busy: false }),
        (e: Error) => live && setState({ value: null, error: e.message, busy: false }),
      );
    }, 350);
    return () => {
      live = false;
      window.clearTimeout(timer);
    };
  }, [model, on]);
  return state;
}

export function Split({ model, actions, view, onView }: { model: StudioModel; actions: Actions; view: View; onView: (v: View) => void }) {
  const mo = useLater<string>(model, actions.source, view === "mo");
  const cpp = useLater<Preview>(model, actions.preview, view === "cpp");
  const [file, setFile] = useState("");
  const names = Object.keys(cpp.value?.files ?? {});
  const shown = names.includes(file) ? file : (names.find((n) => n.endsWith(".cpp") && n.startsWith("domain/")) ?? names[0] ?? "");

  return (
    <section className="split" aria-label="Source">
      <header>
        <div className="switch" role="radiogroup" aria-label="Source view">
          {OPTIONS.map((o) => (
            <button key={o.value} role="radio" aria-checked={o.value === view} className={`opt${o.value === view ? " on" : ""}`} onClick={() => onView(o.value)}>
              {o.label}
            </button>
          ))}
        </div>
      </header>
      {view === "mo" ? (
        <pre className="code" aria-label="Modelica source">{mo.value ?? (mo.error || "…")}</pre>
      ) : (
        <>
          <p className="code-about" role="status">
            {cpp.error ? cpp.error : cpp.value ? <><b>{cpp.value.path}</b> · {cpp.value.reason}</> : "checking…"}
          </p>
          {names.length > 1 && (
            <div className="files" role="tablist" aria-label="Generated files">
              {names.map((n) => (
                <button key={n} role="tab" aria-selected={n === shown} className={n === shown ? "on" : undefined} onClick={() => setFile(n)}>
                  {n}
                </button>
              ))}
            </div>
          )}
          <pre className="code" aria-label="Generated C++">{shown ? cpp.value?.files[shown] : cpp.value && !names.length ? (cpp.value.notices[0] ?? "") : ""}</pre>
        </>
      )}
    </section>
  );
}
