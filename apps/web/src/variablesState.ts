// Variables (#36) as the app holds them: the backend's defaults, then the user's own (kept in the
// project, #34), the sheet's undo, keeping the backend told, and what renaming or removing one does
// to the Flow, the Station and the Board that use it.

import { useCallback, useEffect, useMemo, useRef, useState, type Dispatch, type SetStateAction } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, DashboardLayout, ParamValue, ServerMsg, Station, Variable, Variables } from "./protocol";
import { emptyOf, freeName, merged, refersTo, renameInFormula, renameRef, usesOf, type Use } from "./bindings";
import { mapTexts, renameInText, textUsing } from "./dashboardLayout";
import type { VarsApi } from "./VarsContext";
import { isBlock, type FlowNode } from "./flowModel";

export function useVariables(state: EngineState, send: (m: ClientMsg) => void, subscribe: (f: (m: ServerMsg) => void) => () => void) {
  // Variables (#36): the backend's defaults, then the user's own (kept in the project, #34). The engine resolves them and says what they come to.
  const [mine, setMine] = useState<Variables | undefined>(undefined);
  const engineKey = state.engine || "default";
  useEffect(() => setMine(undefined), [engineKey]); // another backend: its defaults; a project (or the restored session) brings the user's own
  const project = useMemo(() => merged(state.variables, mine), [state.variables, mine]);
  // MX's live variables are shown beside the project's own; the project is what we send back.
  const remote = state.remoteVariables;
  const variables = useMemo(() => {
    const names = new Set(remote.map((v) => v.name));
    return [
      ...project.filter((v) => !names.has(v.name)),
      ...remote.map((v): Variable => ({ ...v, source: "mx", group: v.group ?? "Squero MX" })),
    ];
  }, [project, remote]);
  const varsHistory = useRef<{ past: Variables[]; future: Variables[] }>({ past: [], future: [] });
  const keepVars = setMine;
  const setVariables = useCallback(
    (next: Variables) => {
      varsHistory.current = { past: [...varsHistory.current.past.slice(-50), project], future: [] };
      keepVars(next);
    },
    [project, keepVars],
  );
  // A block set variables during a run (#166): keep them in the project, as if the user had. Not an undo step.
  useEffect(
    () =>
      subscribe((m) => {
        if (m.type === "variables.patch")
          setMine((was) => merged(state.variables, was).map((v) => (!v.expr && v.name in m.values ? { ...v, value: m.values[v.name] } : v)));
      }),
    [subscribe, state.variables],
  );
  // The backend keeps the variables it was last told: tell it when they change.
  const backendVars = useRef("[]");
  useEffect(() => {
    backendVars.current = JSON.stringify(state.variables);
  }, [state.variables, state.engine]);
  useEffect(() => {
    if (state.connection !== "open") return;
    const now = JSON.stringify(project);
    if (now === backendVars.current) return;
    backendVars.current = now;
    send({ type: "variables.set", variables: project });
  }, [project, state.connection, send]);
  const undoVars = (redo: boolean) => {
    const h = varsHistory.current;
    const from = redo ? h.future : h.past;
    const to = from.pop();
    if (!to) return;
    (redo ? h.past : h.future).push(project);
    keepVars(to);
  };
  // `variables` is what the sheet shows (project + live MX); `project` is what a run and a project file take.
  return { variables, project, keepVars, setVariables, varsHistory, undoVars };
}

/** What the Variables sheet and every bound field use. */
export function varsApiFor({
  state,
  variables,
  setVariables,
  varsHistory,
  running,
  nodes,
  setNodes,
  station,
  editStation,
  dashLayout,
  saveDash,
  setOpenVar,
  setVarsShown,
}: {
  state: EngineState;
  variables: Variables;
  setVariables: (next: Variables) => void;
  varsHistory: { current: { past: Variables[]; future: Variables[] } };
  running: boolean;
  nodes: FlowNode[];
  setNodes: Dispatch<SetStateAction<FlowNode[]>>;
  station: Station | null;
  editStation: (next: Station) => void;
  dashLayout: DashboardLayout | undefined;
  saveDash: (next: DashboardLayout | undefined) => void;
  setOpenVar: (name: string | null) => void;
  setVarsShown: (v: "open" | "closed") => void;
}): VarsApi {
  const mapParams = (params: Record<string, ParamValue>, f: (v: ParamValue) => ParamValue) =>
    Object.fromEntries(Object.entries(params).map(([k, v]) => [k, f(v)])) as Record<string, ParamValue>;
  const rewrite = (uses: (v: ParamValue) => boolean, f: (v: ParamValue) => ParamValue) => {
    setNodes((ns) => ns.map((n) => (isBlock(n) && Object.values(n.data.params).some(uses) ? { ...n, data: { ...n.data, params: mapParams(n.data.params, f) } } : n)));
    if (station?.objects.some((o) => Object.values(o.props ?? {}).some(uses)))
      editStation({ objects: station.objects.map((o) => ({ ...o, props: mapParams(o.props ?? {}, f) })) });
  };
  /** Labels and section titles: the same rewrite as in params. Rewriting the backend's default layout makes it the user's own. */
  const rewriteLabels = (f: (text: string) => string) => {
    if (!dashLayout) return;
    const next = mapTexts(dashLayout, f);
    if (next !== dashLayout) saveDash(next);
  };
  return {
    list: variables,
    values: state.varValues,
    error: state.varError,
    defaults: state.variables,
    locked: running,
    setList: (next) => setVariables(next.filter((v) => v.source !== "mx")),
    rename: (from, to) => {
      if (variables.find((v) => v.name === from)?.source === "mx") return;
      setVariables(variables.map((v) => (v.name === from ? { ...v, name: to } : v.expr ? { ...v, expr: renameInFormula(v.expr, from, to) } : v)));
      rewrite((v) => refersTo(v, from), (v) => renameRef(v, from, to));
      rewriteLabels((t) => renameInText(t, from, to));
      varsHistory.current = { past: [], future: [] }; // the Flow and Station changed too: not undone from here
      setOpenVar(to);
    },
    remove: (name) => {
      const gone = variables.find((v) => v.name === name);
      if (!gone || gone.source === "mx") return;
      // Keep the references: a field bound to a removed variable goes red ("No variable
      // called …") instead of silently keeping the last number in its place.
      setVariables(variables.filter((v) => v.name !== name && v.source !== "mx"));
      varsHistory.current = { past: [], future: [] };
      setOpenVar(null);
    },
    uses: (name) => [
      ...usesOf(name, nodes.filter(isBlock).map((n) => ({ id: n.id, params: n.data.params })), station, variables),
      ...textUsing(dashLayout, name).map((d): Use => ({ page: "dashboard", id: d.kind, field: d.text })),
    ],
    reveal: (name) => {
      setVarsShown("open");
      setOpenVar(name);
    },
    promote: (def, value) => {
      const name = freeName(def.id, variables);
      const type: Variable["type"] = def.type === "key" || def.type === "variable" ? "text" : def.type;
      const v: Variable = {
        name,
        type,
        value: typeof value === "object" ? emptyOf(type) : value,
        ...(def.type === "enum" ? { options: def.options } : {}),
        ...(def.type === "number" && def.unit ? { unit: def.unit } : {}),
      };
      setVariables([...variables, v]);
      return name;
    },
  };
}
