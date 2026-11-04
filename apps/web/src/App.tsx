import { useEdgesState, useNodesState, useReactFlow } from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useEngine } from "./engine";
import { BrowserWindow, DRAG_TYPE, dragging, type Item } from "./Browser";
import { FlowEditor, fromGraph, isBlock, portsOf, resolveBlocks, RunStates, syncLinked, toGraph, withBlock, type FlowControls, type FlowNode, type Wire } from "./FlowEditor";
import { absolute, isFrame, levelOf } from "./groups";
import type { DashboardLayout, ParamValue, RunSettings, Station, Variable, Variables } from "./protocol";
import { VariablesSheet, VarsContext, type VarsApi } from "./Variables";
import { emptyOf, freeName, inlineRef, loadMine, merged, refersTo, renameInFormula, renameRef, saveMine, toToml, usesOf, type Use } from "./bindings";
import { ConsoleWindow } from "./Console";
import { Dashboard } from "./Dashboard";
import { inlineInText, loadSaved, mapTexts, renameInText, save as saveLayout, textUsing } from "./dashboardLayout";
import { PAGES, Panel, Rail, Switch, Wordmark, type Mode, type Page, type Theme } from "./Shell";
import { SceneView } from "./SceneView";
import { StationGraph } from "./StationGraph";
import { StationPanel, withObject } from "./StationPanel";
import { BackendWindow, Dock } from "./Dock";
import { CommandSearch, MenuBar } from "./Menus";
import { ownKeys, SEARCH_CHORD, useShortcuts, type Command } from "./commands";
import { useUndo } from "./undo";
import { WebStation } from "./WebStation";

const DEFAULT_URL =
  new URLSearchParams(location.search).get("backend") ??
  import.meta.env.VITE_ENGINE_URL ??
  "ws://localhost:8765";

/** A per-viewer convenience, kept in this browser when it can be. */
function remembered<T extends string>(key: string, fallback: T, allowed: readonly T[]): [T, (v: T) => void] {
  const [value, setValue] = useState<T>(() => {
    try {
      const v = localStorage.getItem(key) as T | null;
      return v && allowed.includes(v) ? v : fallback;
    } catch {
      return fallback;
    }
  });
  const set = (v: T) => {
    setValue(v);
    try {
      localStorage.setItem(key, v);
    } catch {
      /* private window: not kept */
    }
  };
  return [value, set];
}

const PAGE_IDS = PAGES.map((p) => p.id);

/** "1 object", "2 objects". */
const count = (n: number, one: string, many = `${one}s`) => `${n} ${n === 1 ? one : many}`;

/** The theme in use: the user's pick, or the system's. Sets html[data-theme]. */
function useTheme(choice: Theme): "light" | "dark" {
  const query = "(prefers-color-scheme: dark)";
  const [system, setSystem] = useState(() => (window.matchMedia?.(query).matches ? "dark" : "light"));
  useEffect(() => {
    const m = window.matchMedia?.(query);
    if (!m) return;
    const on = () => setSystem(m.matches ? "dark" : "light");
    m.addEventListener("change", on);
    return () => m.removeEventListener("change", on);
  }, []);
  useEffect(() => {
    if (choice === "auto") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = choice;
  }, [choice]);
  return choice === "auto" ? (system as "light" | "dark") : choice;
}

export function App() {
  const [mode, setMode] = remembered<Mode>("paperboat.mode", "navigator", ["navigator", "commander"]);
  const [arrange, setArrange] = useState(false);
  // The floor grid of a 3D Station: the user's switch wins, else the backend's `view.grid`, else shown.
  const [gridChoice, setGridChoice] = remembered<"auto" | "on" | "off">("paperboat.grid", "auto", ["auto", "on", "off"]);
  const [page, setPage] = remembered<Page>("paperboat.page", "flow", PAGE_IDS);
  const [themeChoice, setThemeChoice] = remembered<Theme>("paperboat.theme", "auto", ["auto", "light", "dark"]);
  const theme = useTheme(themeChoice);
  const [url, setUrl] = useState(DEFAULT_URL);
  const [searchOpen, setSearchOpen] = useState(false); // the command search (Ctrl+K)
  const [backendOpen, setBackendOpen] = useState(false); // File > Backend…
  const flow = useRef<FlowControls | null>(null); // the canvas's actions, for the Edit menu
  const { state, send, subscribe } = useEngine(url);
  const [nodes, setNodes, onNodesChange] = useNodesState<FlowNode>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Wire>([]);
  const { fitView, screenToFlowPosition } = useReactFlow();
  const [station, setStation] = useState<Station | null>(null);
  const [runSettings, setRunSettings] = useState<RunSettings>({});
  // Variables (#36): the backend's defaults, then the user's own (kept in this browser per
  // backend until projects, #34). The engine resolves them and says what they come to.
  const [mine, setMine] = useState<Variables | undefined>(undefined);
  const engineKey = state.engine || "default";
  useEffect(() => setMine(loadMine(engineKey)), [engineKey]);
  const variables = useMemo(() => merged(state.variables, mine), [state.variables, mine]);
  // The Dashboard's layout is kept here, so renaming or deleting a variable can rewrite the labels that use it (#86).
  const [dashSaved, setDashSaved] = useState(() => loadSaved(engineKey));
  useEffect(() => setDashSaved(loadSaved(engineKey)), [engineKey]);
  const saveDash = (next: DashboardLayout | undefined) => {
    setDashSaved(next);
    saveLayout(engineKey, next);
  };
  const dashLayout = dashSaved ?? state.dashboard;
  const [varsShown, setVarsShown] = remembered<"open" | "closed">("paperboat.variables", "closed", ["open", "closed"]);
  const [openVar, setOpenVar] = useState<string | null>(null);
  const [consoleShown, setConsoleShown] = useState(false);
  const varsHistory = useRef<{ past: Variables[]; future: Variables[] }>({ past: [], future: [] });
  const keepVars = useCallback((next: Variables) => {
    setMine(next);
    saveMine(engineKey, next);
  }, [engineKey]);
  const setVariables = useCallback(
    (next: Variables) => {
      varsHistory.current = { past: [...varsHistory.current.past.slice(-50), variables], future: [] };
      keepVars(next);
    },
    [variables, keepVars],
  );
  // The backend keeps the variables it was last told: tell it when they change.
  const backendVars = useRef("[]");
  useEffect(() => {
    backendVars.current = JSON.stringify(state.variables);
  }, [state.variables, state.engine]);
  useEffect(() => {
    if (state.connection !== "open") return;
    const now = JSON.stringify(variables);
    if (now === backendVars.current) return;
    backendVars.current = now;
    send({ type: "variables.set", variables });
  }, [variables, state.variables, state.connection, send]);
  useEffect(() => setRunSettings({}), [state.engine]); // another backend: its own defaults

  // What the canvas and station were loaded as, and from which backend: a backend that
  // comes back (restarted, reconnected) doesn't overwrite what the user changed since.
  const canvas = useRef({ engine: "", graph: "", station: "" });
  const current = useRef({ nodes, edges, station });
  current.current = { nodes, edges, station };

  // The backend decides what the station opens with (station.load); edits go back to it.
  useEffect(() => {
    const mine = current.current.station;
    const edited = mine && canvas.current.engine === state.engine && JSON.stringify(mine) !== canvas.current.station;
    if (edited) {
      send({ type: "station.set", station: mine }); // keep the user's station; tell the backend
      return;
    }
    setStation(state.station);
    canvas.current.station = JSON.stringify(state.station);
  }, [state.station, state.engine, send]);
  const editStation = useCallback(
    (next: Station) => {
      setStation(next);
      send({ type: "station.set", station: next });
    },
    [send],
  );

  // Linked blocks follow the station: one per object that spawns one.
  useEffect(() => {
    setNodes((ns) => syncLinked(ns, station, state.objects, state.blocks));
  }, [station, state.objects, state.blocks, setNodes]);
  // Wires to a block that's gone (its station object was removed), or to a port it no
  // longer has (a magic block switched form), go too.
  useEffect(() => {
    const ports = new Map<string, Set<string> | null>(
      nodes.map((n) => [n.id, isBlock(n) && !n.data.missing ? new Set([...portsOf(n).inputs, ...portsOf(n).outputs].map((p) => p.id)) : null]),
    );
    const has = (id: string, port: string | null | undefined) => {
      if (!ports.has(id)) return false;
      const p = ports.get(id);
      return p === null || p === undefined || p.has(port ?? "");  // groups and missing blocks keep their wires
    };
    setEdges((es) => {
      const kept = es.filter((e) => has(e.source, e.sourceHandle) && has(e.target, e.targetHandle));
      return kept.length === es.length ? es : kept;
    });
  }, [nodes, setEdges]);

  // The backend decides what the canvas opens with (graph.load), unless the user changed
  // the canvas since this same backend loaded it: then the canvas stays, checked against the
  // (perhaps new) catalog, so a block that's gone shows as missing instead of vanishing.
  useEffect(() => {
    const now = current.current;
    const edited =
      now.nodes.length > 0 &&
      canvas.current.engine === state.engine &&
      JSON.stringify(toGraph(now.nodes, now.edges)) !== canvas.current.graph;
    if (edited) {
      setNodes((ns) => resolveBlocks(ns, now.edges, state.blocks));
      return;
    }
    const g = state.loaded ? fromGraph(state.loaded, state.blocks) : { nodes: [], edges: [] };
    setNodes(g.nodes);
    setEdges(g.edges);
    canvas.current = { ...canvas.current, engine: state.engine, graph: JSON.stringify(toGraph(g.nodes, g.edges)) };
    requestAnimationFrame(() => fitView());
  }, [state.loaded, state.blocks, state.engine, setNodes, setEdges, fitView]);

  // Ctrl+Z / Ctrl+Shift+Z on the Flow canvas; a new graph from the backend starts afresh.
  const history = useUndo(nodes, edges, setNodes, setEdges, state.loaded);

  // A page that changes size (another mode, or brought into view) fits its flow again.
  useEffect(() => {
    if (mode === "navigator" && page !== "flow") return;
    const t = window.setTimeout(() => fitView({ duration: 200 }), 60);
    return () => window.clearTimeout(t);
  }, [mode, page, fitView]);

  // (Alt+1–3, Alt+M, Alt+V and Ctrl+K are commands now: see `commands` below.)

  useEffect(() => {
    if (!consoleShown) return;
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && setConsoleShown(false);
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [consoleShown]);

  // Selecting a variable unselects the blocks and wires, so their actions don't cover its own (#79).
  useEffect(() => {
    if (!openVar) return;
    setNodes((ns) => (ns.some((n) => n.selected) ? ns.map((n) => (n.selected ? { ...n, selected: false } : n)) : ns));
    setEdges((es) => (es.some((e) => e.selected) ? es.map((e) => (e.selected ? { ...e, selected: false } : e)) : es));
  }, [openVar, setNodes, setEdges]);

  // Forward the keyboard to the backend (e.g. a Key block). Typing in a field is not input.
  useEffect(() => {
    const forward = (down: boolean) => (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || ownKeys(e.target) || e.repeat || e.defaultPrevented) return; // taken by the app (quick add's /, a menu's arrows)
      if (e.code === "Space") e.preventDefault(); // don't click the focused button
      send({ type: "input", key: e.code, down });
    };
    const onDown = forward(true);
    const onUp = forward(false);
    window.addEventListener("keydown", onDown);
    window.addEventListener("keyup", onUp);
    return () => {
      window.removeEventListener("keydown", onDown);
      window.removeEventListener("keyup", onUp);
    };
  }, [send]);

  const running = state.run?.status === "running";
  const runStates = useMemo(
    () => ({ active: new Set(state.active), failed: new Set(state.failed), progress: state.progress }),
    [state.active, state.failed, state.progress],
  );

  // The subsystem or story open on the Flow canvas (#68); null: the top level.
  const [flowLevel, setFlowLevel] = useState<string | null>(null);

  // A Console line names a block: open the level it is on, select it and bring it into view.
  const showBlock = (id: string) => {
    const node = nodes.find((n) => n.id === id);
    if (!node) return;
    setPage("flow");
    setFlowLevel(levelOf(id, nodes));
    // Open the frame it's in, if that's collapsed.
    setNodes((ns) =>
      ns.map((n) =>
        n.id === node.parentId && isFrame(n) && n.data.kind === "visual" ? { ...n, data: { ...n.data, collapsed: false } } : { ...n, selected: n.id === id },
      ),
    );
    window.setTimeout(() => fitView({ nodes: [{ id }], duration: 300, maxZoom: 1.2 }), 80);
  };

  const view = state.stationView.view;

  // ---- Variables: what the sheet and every bound field use ----
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
  const varsApi: VarsApi = {
    list: variables,
    values: state.varValues,
    error: state.varError,
    defaults: state.variables,
    locked: running,
    setList: setVariables,
    rename: (from, to) => {
      setVariables(variables.map((v) => (v.name === from ? { ...v, name: to } : v.expr ? { ...v, expr: renameInFormula(v.expr, from, to) } : v)));
      rewrite((v) => refersTo(v, from), (v) => renameRef(v, from, to));
      rewriteLabels((t) => renameInText(t, from, to));
      varsHistory.current = { past: [], future: [] }; // the Flow and Station changed too: not undone from here
      setOpenVar(to);
    },
    remove: (name) => {
      const gone = variables.find((v) => v.name === name);
      const value = state.varValues[name] ?? gone?.value ?? 0;
      rewrite((v) => refersTo(v, name), (v) => inlineRef(v, name, value));
      rewriteLabels((t) => inlineInText(t, name, value));
      setVariables(
        variables
          .filter((v) => v.name !== name)
          .map((v) => (v.expr && refersTo({ expr: v.expr }, name) ? { ...v, expr: (inlineRef({ expr: v.expr }, name, value) as { expr: string }).expr } : v)),
      );
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
      const type: Variable["type"] = def.type === "key" ? "text" : def.type;
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
  const undoVars = (redo: boolean) => {
    const h = varsHistory.current;
    const from = redo ? h.future : h.past;
    const to = from.pop();
    if (!to) return;
    (redo ? h.past : h.future).push(variables);
    keepVars(to);
  };
  const showUse = (u: Use) => {
    if (u.page === "variables") setOpenVar(u.id);
    else if (u.page === "dashboard") setPage("dashboard");
    else if (u.page === "flow") showBlock(u.id);
    else setPage("station");
  };

  // Adding (#50): the page's Add key opens the collection as a floating window.
  const [browser, setBrowser] = useState<"flow" | "station" | null>(null);
  const blockItems = useMemo<Item[]>(() => state.blocks.filter((b) => !b.linked).map((def) => ({ kind: "block", def })), [state.blocks]);
  const objectItems = useMemo<Item[]>(() => {
    const defs = new Map(state.blocks.map((b) => [b.type, b]));
    return state.objects.map((def) => ({ kind: "object", def, linked: def.block ? defs.get(def.block) : undefined }));
  }, [state.objects, state.blocks]);
  const counts = (list: { type: string }[]) => list.reduce<Record<string, number>>((c, x) => ((c[x.type] = (c[x.type] ?? 0) + 1), c), {});
  const taken = new Set([...(station?.objects ?? []).map((o) => o.id), ...nodes.map((n) => n.id)]);
  const addObject = (item: Item, pos?: [number, number]) => {
    if (item.kind !== "object" || running) return;
    editStation(withObject(station ?? { objects: [] }, item.def, taken, pos));
  };
  const addBlock = (item: Item) => {
    if (item.kind !== "block") return;
    // In the middle of the canvas as it is in view.
    const r = document.querySelector(".panel-flow .react-flow")?.getBoundingClientRect();
    const at = screenToFlowPosition({ x: r ? r.left + r.width / 2 : 400, y: r ? r.top + r.height / 2 : 300 });
    setNodes((ns) => withBlock(ns, item.def, { x: at.x - 78 + (ns.length % 5) * 18, y: at.y - 30 + (ns.length % 5) * 18 }, flowLevel)[0]);
    if (mode === "navigator") setPage("flow");
  };
  const gridOn = gridChoice === "auto" ? (state.view.mode === "3d" ? state.view.grid !== false : true) : gridChoice === "on";
  const gridKey =
    view === "scene" && state.view.mode === "3d" ? (
      <button className={`key small${gridOn ? " on" : ""}`} aria-pressed={gridOn} title="Show or hide the floor grid" onClick={() => setGridChoice(gridOn ? "off" : "on")}>
        Grid
      </button>
    ) : null;
  const addKey = (which: "flow" | "station") => (
    <button className="key small add" disabled={which === "station" && (running || state.objects.length === 0)} onClick={() => setBrowser(browser === which ? null : which)}>
      + Add
    </button>
  );
  const monitors = useMemo(
    () => Object.fromEntries(nodes.filter(isBlock).filter((n) => n.data.def.kind === "monitor").map((n) => [n.id, { kind: n.data.def.instrument ?? "graph", ...absolute(n, nodes) }])),
    [nodes],
  );
  const errors = state.logs.filter((l) => l.level === "error").length;
  const open = (p: Page) => {
    setPage(p);
    setMode("navigator");
  };
  const panel = (p: Page) => ({ page: p, mode, current: page === p, onOpen: () => open(p) });
  const run = () =>
    send({
      type: "graph.run",
      graph: toGraph(nodes, edges),
      ...(station ? { station } : {}),
      ...(Object.keys(runSettings).length ? { settings: runSettings } : {}),
      variables,
    });

  const status = state.run?.status;
  const elapsed = state.run ? `${(state.run.progress * (Number(runSettings.duration ?? state.runDefaults.duration) || 0)).toFixed(1)} / ${runSettings.duration ?? state.runDefaults.duration ?? "?"} s` : "";

  // ---- The commands (#78): what the menus, the command search and the shortcuts all run ----
  // Only what the app already does; backends' commands and the file actions come with their issues.
  const canRun = state.connection === "open" && !running;
  const flowInView = mode === "commander" || page === "flow";
  /** An Edit action on the canvas: on while the Flow is in view and the canvas allows it. */
  const onCanvas = (can: (c: FlowControls["can"]) => boolean) => () => flowInView && !!flow.current && can(flow.current.can);
  const canvasWhy = (why: string) => (flowInView ? why : "Open the Flow page first");
  const commands: Command[] = [
    { id: "file.backend", label: "Backend…", menu: "file", group: "backend", run: () => setBackendOpen(true) },

    { id: "edit.undo", label: "Undo", menu: "edit", group: "history", shortcut: "Ctrl+Z", enabled: history.canUndo, why: "Nothing to undo on the Flow canvas", run: history.undo },
    { id: "edit.redo", label: "Redo", menu: "edit", group: "history", shortcut: "Ctrl+Shift+Z", enabled: history.canRedo, why: "Nothing to redo", run: history.redo },
    { id: "edit.cut", label: "Cut", menu: "edit", group: "clipboard", shortcut: "Ctrl+X", enabled: onCanvas((c) => c.copy), why: canvasWhy("Select blocks first"), run: () => flow.current?.cut() },
    { id: "edit.copy", label: "Copy", menu: "edit", group: "clipboard", shortcut: "Ctrl+C", enabled: onCanvas((c) => c.copy), why: canvasWhy("Select blocks first"), run: () => flow.current?.copy() },
    { id: "edit.paste", label: "Paste", menu: "edit", group: "clipboard", shortcut: "Ctrl+V", enabled: onCanvas((c) => c.paste), why: canvasWhy("Copy blocks first"), run: () => flow.current?.paste() },
    { id: "edit.delete", label: "Delete", menu: "edit", group: "clipboard", shortcut: "Del", enabled: onCanvas((c) => c.remove), why: canvasWhy("Select blocks or wires first"), run: () => flow.current?.remove() },
    { id: "edit.group", label: "Group", menu: "edit", group: "grouping", enabled: onCanvas((c) => c.group), why: canvasWhy("Select blocks that sit in the same place"), run: () => flow.current?.group("subsystem") },
    { id: "edit.story", label: "Story", menu: "edit", group: "grouping", enabled: onCanvas((c) => c.story), why: canvasWhy("A story holds signal blocks only: no step ports"), run: () => flow.current?.group("story") },
    { id: "edit.frame", label: "Frame", menu: "edit", group: "grouping", enabled: onCanvas((c) => c.frame), why: canvasWhy("Select blocks first"), run: () => flow.current?.group("visual") },
    { id: "edit.ungroup", label: "Ungroup", menu: "edit", group: "grouping", enabled: onCanvas((c) => c.ungroup), why: canvasWhy("Select a group first"), run: () => flow.current?.ungroup() },
    { id: "edit.addBlock", label: "Add block…", menu: "edit", group: "add", shortcut: "/", run: () => setBrowser("flow") },
    {
      id: "edit.addObject",
      label: "Add station object…",
      menu: "edit",
      group: "add",
      enabled: !running && state.objects.length > 0,
      why: running ? "Stop the run to edit the station" : "This backend has no station objects",
      run: () => setBrowser("station"),
    },
    { id: "edit.variables", label: "Variables…", menu: "edit", group: "panels", shortcut: "Alt+V", chord: { key: "v", alt: true }, meta: String(variables.length), run: () => setVarsShown(varsShown === "open" ? "closed" : "open") },

    { id: "view.search", label: "Command search…", menu: "view", group: "search", shortcut: "Ctrl+K", chord: SEARCH_CHORD, quiet: true, run: () => setSearchOpen(true) },
    ...PAGES.map((p, i): Command => ({
      id: `view.page.${p.id}`,
      label: p.label,
      menu: "view",
      group: "pages",
      shortcut: `Alt+${i + 1}`,
      chord: { key: String(i + 1), alt: true },
      checked: mode === "navigator" && page === p.id,
      run: () => open(p.id),
    })),
    { id: "view.mode.navigator", label: "Navigator", menu: "view", group: "mode", checked: mode === "navigator", run: () => setMode("navigator") },
    { id: "view.mode.commander", label: "Commander", menu: "view", group: "mode", checked: mode === "commander", run: () => setMode("commander") },
    { id: "view.mode.switch", label: "Switch view mode", menu: "view", group: "mode", shortcut: "Alt+M", chord: { key: "m", alt: true }, run: () => setMode(mode === "navigator" ? "commander" : "navigator") },
    { id: "view.theme.auto", label: "Auto", menu: "view", group: "theme", checked: themeChoice === "auto", run: () => setThemeChoice("auto") },
    { id: "view.theme.light", label: "Light", menu: "view", group: "theme", checked: themeChoice === "light", run: () => setThemeChoice("light") },
    { id: "view.theme.dark", label: "Dark", menu: "view", group: "theme", checked: themeChoice === "dark", run: () => setThemeChoice("dark") },
    { id: "view.console", label: "Console", menu: "view", group: "windows", checked: consoleShown, meta: errors > 0 ? count(errors, "error") : undefined, run: () => setConsoleShown(!consoleShown) },
    {
      id: "view.grid",
      label: "Floor grid",
      menu: "view",
      group: "windows",
      checked: gridOn,
      enabled: view === "scene" && state.view.mode === "3d",
      why: "The Station is not a 3D scene",
      run: () => setGridChoice(gridOn ? "off" : "on"),
    },
    {
      id: "view.arrange",
      label: "Arrange Dashboard",
      menu: "view",
      group: "windows",
      checked: arrange,
      run: () => {
        if (!arrange && mode === "navigator") setPage("dashboard");
        setArrange(!arrange);
      },
    },

    { id: "run.run", label: "Run", menu: "run", group: "run", enabled: canRun, why: running ? "A run is in progress" : "Not connected to a backend", run },
    { id: "run.cancel", label: "Cancel", menu: "run", group: "run", enabled: running, why: "No run in progress", run: () => send({ type: "run.cancel" }) },
  ];
  useShortcuts(commands);

  return (
    <div className={`app mode-${mode}`}>
      <header className="bar">
        <Wordmark />
        <MenuBar commands={commands} />
        <span className="spacer" />
        {errors > 0 && (
          <button className="key small err" title="Open the Console" onClick={() => setConsoleShown(true)}>
            {count(errors, "error")}
          </button>
        )}
        <Switch
          label="View"
          value={mode}
          onChange={setMode}
          options={[
            { value: "navigator", label: "navigator", hint: "One page at a time (Alt+M)" },
            { value: "commander", label: "commander", hint: "Every page at once (Alt+M)" },
          ]}
        />
        <button className={`chip conn-${state.connection}`} title="Which backend: File > Backend…" onClick={() => setBackendOpen(true)}>
          <i aria-hidden="true" />
          {state.connection === "open" ? state.engine : state.connection}
        </button>
      </header>
      {mode === "navigator" && <Rail page={page} onPage={setPage} />}
      <VarsContext.Provider value={varsApi}>
      <main className={`pages${varsShown === "open" ? " with-vars" : ""}`}>
        <Panel {...panel("station")} actions={<>{gridKey}{addKey("station")}</>} info={station ? `${count(station.objects.length, "object")} · ${view === "scene" ? state.view.mode : view}` : view}>
          <div
            className={`station view-${view}`}
            onDragOver={(e) => {
              if (!e.dataTransfer.types.includes(DRAG_TYPE) || dragging.current?.kind !== "object") return;
              e.preventDefault();
              e.dataTransfer.dropEffect = "copy";
            }}
            onDrop={(e) => {
              const it = dragging.current;
              if (!it || it.kind !== "object") return;
              e.preventDefault();
              dragging.current = null;
              addObject(it);
            }}
          >
            {view === "web" && state.stationView.page ? (
              <WebStation
                backend={url}
                page={state.stationView.page}
                state={state}
                subscribe={subscribe}
                send={send}
                onStation={editStation}
              />
            ) : view === "graph" ? (
              <StationGraph objects={state.objects} station={station ?? { objects: [] }} onChange={editStation} locked={running} />
            ) : (
              <SceneView key={`${theme}:${JSON.stringify(state.view)}`} view={state.view} grid={gridOn} entities={state.entities} />
            )}
            {station && state.objects.length > 0 && (
              <StationPanel
                objects={state.objects}
                station={station}
                taken={taken}
                onChange={editStation}
                locked={running}
              />
            )}
          </div>
        </Panel>
        <Panel {...panel("flow")} actions={addKey("flow")} info={count(nodes.filter(isBlock).length, "block")}>
          <RunStates.Provider value={runStates}>
            <FlowEditor
              blocks={state.blocks}
              nodes={nodes}
              edges={edges}
              setNodes={setNodes}
              setEdges={setEdges}
              onNodesChange={onNodesChange}
              onEdgesChange={onEdgesChange}
              minimap={mode === "navigator" && page === "flow"}
              controls={flow}
              level={flowLevel}
              setLevel={setFlowLevel}
            />
          </RunStates.Provider>
        </Panel>
        <Panel
          {...panel("dashboard")}
          actions={
            <button className={`key small${arrange ? " on" : ""}`} aria-pressed={arrange} title="Move instruments and pick their sizes" onClick={() => setArrange(!arrange)}>
              {arrange ? "Done" : "Arrange"}
            </button>
          }
          info={count(Object.keys(monitors).length, "instrument")}
        >
          <Dashboard state={state} monitors={monitors} arrange={arrange} onBlock={showBlock} saved={dashSaved} onSave={saveDash} />
        </Panel>
        {varsShown === "open" && (
          <VariablesSheet
            open={openVar}
            onOpen={setOpenVar}
            onClose={() => setVarsShown("closed")}
            onShow={showUse}
            onUndo={undoVars}
            toml={() => toToml(variables)}
          />
        )}
        <Dock
          canRun={canRun}
          whyNot={running ? "A run is in progress" : "Not connected to a backend"}
          running={running}
          status={state.run ? status : undefined}
          statusText={status === "running" ? `running ${elapsed}` : status}
          defaults={state.runDefaults}
          settings={runSettings}
          onSettings={setRunSettings}
          onRun={run}
          onCancel={() => send({ type: "run.cancel" })}
        />
      </main>
      </VarsContext.Provider>
      {searchOpen && <CommandSearch commands={commands} onClose={() => setSearchOpen(false)} />}
      {backendOpen && <BackendWindow url={url} connection={state.connection} engine={state.engine} onConnect={setUrl} onClose={() => setBackendOpen(false)} />}
      {consoleShown && <ConsoleWindow logs={state.logs} errors={errors} onBlock={showBlock} onClose={() => setConsoleShown(false)} />}
      {browser === "flow" && (
        <BrowserWindow title="Add a block" items={blockItems} counts={counts(nodes.filter(isBlock).map((n) => ({ type: n.data.def.type })))} place="canvas" onAdd={addBlock} onClose={() => setBrowser(null)} />
      )}
      {browser === "station" && (
        <BrowserWindow
          title="Add to the station"
          items={objectItems}
          counts={counts(station?.objects ?? [])}
          place="station"
          onAdd={(it) => addObject(it)}
          onClose={() => setBrowser(null)}
        />
      )}
    </div>
  );

}
