import { useEffect, useRef, useState } from "react";
import { analysisPanel, analysisRequest, type AnalysisTarget } from "./analyses";
import type { AnalysisDef, ClientMsg, Graph, ServerMsg, Variables } from "./protocol";
import { RemoveIcon } from "./RemoveIcon";

/**
 * The Analysis drawer (pb#370): one toolkit analysis of a subsystem or a selection, docked at
 * the pages' right edge. It asks the backend (analysis.request) and asks again when the part,
 * its params, the variables or the panel's options change; the toolkit's panel draws the
 * answer. The app knows no analysis: the panel comes from the toolkit that registered it.
 */
export function AnalysisDrawer({
  offered,
  name,
  onPick,
  target,
  graph,
  variables,
  send,
  subscribe,
  onClose,
}: {
  offered: AnalysisDef[];
  name: string;
  onPick: (name: string) => void;
  target: AnalysisTarget;
  /** The whole canvas as a graph (live variables in place); the request keeps the target's part. */
  graph: Graph;
  variables: Variables;
  send: (m: ClientMsg) => void;
  subscribe: (listen: (m: ServerMsg) => void) => () => void;
  onClose: () => void;
}) {
  const analysis = offered.find((a) => a.name === name);
  const Panel = analysisPanel(name);
  const [options, setOptions] = useState<Record<string, unknown>>({});
  const [answer, setAnswer] = useState<{ result?: unknown; error?: string }>({});
  const [busy, setBusy] = useState(false);
  const latest = useRef("");
  const counter = useRef(0);

  useEffect(() => setOptions({}), [name]);
  useEffect(
    () =>
      subscribe((m) => {
        if (m.type !== "analysis.result" || m.id !== latest.current) return; // an older request's
        setAnswer({ result: m.result, error: m.error });
        setBusy(false);
      }),
    [subscribe],
  );
  // Ask again when anything it depends on changes, once the edits settle.
  const key = JSON.stringify([name, target, graph, variables, options]);
  useEffect(() => {
    const timer = window.setTimeout(() => {
      const id = `analysis-${++counter.current}`;
      latest.current = id;
      setBusy(true);
      send({ ...analysisRequest(id, name, graph, target, options), variables });
    }, 200);
    return () => window.clearTimeout(timer);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- `key` stands for them all
  }, [key, send]);

  return (
    <section className="panel analysis-drawer" aria-label="Analysis">
      <header className="panel-head">
        <span className="analysis-glyph mono" aria-hidden="true">∿</span>
        {offered.length > 1 ? (
          <select className="well analysis-pick" value={name} onChange={(e) => onPick(e.target.value)} aria-label="Analysis">
            {offered.map((a) => (
              <option key={a.name} value={a.name}>
                {a.label}
              </option>
            ))}
          </select>
        ) : (
          <h2>{analysis?.label ?? name}</h2>
        )}
        <span className="panel-info lbl mono" title={`${target.ids.length} blocks · ${target.inputs.length} in · ${target.outputs.length} out`}>
          {target.label}
          {busy ? " · …" : ""}
        </span>
        <button className="remove" title="Close the analysis" aria-label="Close the analysis" onClick={onClose}>
          <RemoveIcon />
        </button>
      </header>
      <div className="panel-body analysis-body">
        {target.ids.length === 0 ? (
          <p className="muted">Nothing to analyse: pick blocks or a subsystem.</p>
        ) : Panel && analysis ? (
          <Panel analysis={analysis} target={target} result={answer.result} error={answer.error} busy={busy} options={options} setOptions={setOptions} />
        ) : (
          <p className="muted">No toolkit shows "{name}" here.</p>
        )}
      </div>
    </section>
  );
}
