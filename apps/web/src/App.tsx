import { useEdgesState, useNodesState, useReactFlow } from "@xyflow/react";
import { Suspense, lazy, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useEngine } from "./engine";
import { BrowserWindow, DRAG_TYPE, dragging, type Item } from "./Browser";
import { FlowEditor, fromGraph, isBlock, portsOf, resolveBlocks, RunStates, syncLinked, toFlow, toGraph, withBlock, type FlowControls, type FlowNode, type Wire } from "./FlowEditor";
import { absolute, isFrame, levelOf } from "./groups";
import type { DashboardLayout, ParamValue, Project, RunSettings, Station, ToolkitStatus, Variable, Variables } from "./protocol";
import { addRecent, ago, buildProject, clearAutosave, loadAutosave, loadRecent, parseProject, projectFileName, projectName, saveAutosave, saveRecent, stringify, type Stored } from "./project";
import { draggingFiles, droppedText, pickText, saveText, type FileHandle } from "./projectFiles";
import { Notice, ProjectLabel, Toast, type NoticeSpec } from "./ProjectUI";
import { VariablesSheet, VarsContext, type VarsApi } from "./Variables";
import { emptyOf, freeName, inlineRef, loadMine, merged, refersTo, renameInFormula, renameRef, saveMine, toToml, usesOf, type Use } from "./bindings";
import { ConsoleWindow } from "./Console";
import { Dashboard } from "./Dashboard";
import { inlineInText, loadSaved, mapTexts, renameInText, save as saveLayout, textUsing } from "./dashboardLayout";
import { MODES, PAGES, Panel, Rail, Switch, Wordmark, type Drawer, type Mode, type Page, type Theme } from "./Shell";
const SceneView = lazy(() => import("./SceneView").then((m) => ({ default: m.SceneView })));
import { StationGraph, type StationControls } from "./StationGraph";
import { StationPanel, withObject } from "./StationPanel";
import { BackendWindow, Dock } from "./Dock";
import { CommandSearch, MenuBar } from "./Menus";
import { ownKeys, SEARCH_CHORD, useShortcuts, type Command } from "./commands";
import { missingRequiredToolkits, MissingToolkitsDialog, ToolkitsButton } from "./Toolkits";
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

/** A drawer's open state and size, kept in this browser. */
function useDrawer(key: string, open: boolean, size: number, side: Drawer["side"]): Drawer & { setOpen: (o: boolean) => void } {
  const [isOpen, setIsOpen] = remembered<"open" | "closed">(`paperboat.${key}`, open ? "open" : "closed", ["open", "closed"]);
  const [px, setPx] = useState(() => {
    try {
      return Number(localStorage.getItem(`paperboat.${key}Size`)) || size;
    } catch {
      return size;
    }
  });
  return {
    side,
    open: isOpen === "open",
    size: px,
    setOpen: (o) => setIsOpen(o ? "open" : "closed"),
    onChange: (o, next) => {
      setIsOpen(o ? "open" : "closed");
      if (!next) return;
      setPx(next);
      try {
        localStorage.setItem(`paperboat.${key}Size`, String(Math.round(next)));
      } catch {
        /* private window: not kept */
      }
    },
  };
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
  const [mode, setMode] = remembered<Mode>("paperboat.mode", "navigator", MODES);
  // The drawers: Commander's Board under Station and Flow; Lookout's Station and Flow beside the Board.
  const boardDrawer = useDrawer("board", false, 300, "bottom");
  const stationDrawer = useDrawer("lookoutStation", false, 420, "left");
  const flowDrawer = useDrawer("lookoutFlow", false, 520, "right");
  const [arrange, setArrange] = useState(false);
  // The floor grid of a 3D Station: the user's switch wins, else the backend's `view.grid`, else shown.
  const [gridChoice, setGridChoice] = remembered<"auto" | "on" | "off">("paperboat.grid", "auto", ["auto", "on", "off"]);
  const [page, setPage] = remembered<Page>("paperboat.page", "flow", PAGE_IDS);
  const [themeChoice, setThemeChoice] = remembered<Theme>("paperboat.theme", "auto", ["auto", "light", "dark"]);
  const theme = useTheme(themeChoice);
  const [url, setUrl] = useState(DEFAULT_URL);
  const [searchOpen, setSearchOpen] = useState(false); // the command search (Ctrl+K)
  const [backendOpen, setBackendOpen] = useState(false); // File > Backend…
  const flow = useRef<FlowControls | null>(null); // the canvas's actions, for the menus
  const stationCanvas = useRef<StationControls | null>(null);
  const { state, send, subscribe, log } = useEngine(url);
  const [nodes, setNodes, onNodesChange] = useNodesState<FlowNode>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Wire>([]);
  const { fitView, screenToFlowPosition } = useReactFlow();
  const [station, setStation] = useState<Station | null>(null);
  const [runSettings, setRunSettings] = useState<RunSettings>({});
  // Variables (#36): the backend's defaults, then the user's own (kept in this browser per
  // backend as the working copy; a project file, #82, saves them). The engine resolves them and says what they come to.
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
    // After the rail has stepped aside or the column has slid (both about half a second).
    const t = window.setTimeout(() => fitView({ duration: 200 }), 520);
    return () => window.clearTimeout(t);
  }, [mode, page, boardDrawer.open, flowDrawer.open, fitView]);

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
  const arrangeCanvas = (which: "flow" | "station"): boolean => {
    if (running) return false;
    return (which === "flow" ? flow.current : stationCanvas.current)?.arrange() ?? false;
  };
  const arrangeKey = (which: "flow" | "station") => {
    if (which === "station" && view !== "graph") return null;
    const available = !!(which === "flow" ? flow.current : stationCanvas.current)?.can.arrange;
    const title = running ? "Stop the run to arrange blocks" : available ? "Arrange left to right, following the wires" : "There are no blocks to arrange";
    return (
      <button className="key small" disabled={running || !available} title={title} onClick={() => arrangeCanvas(which)}>
        Arrange
      </button>
    );
  };
  const monitors = useMemo(
    () =>
      Object.fromEntries(
        nodes
          .filter(isBlock)
          .filter((n) => n.data.def.kind === "monitor" || !!n.data.def.instrument)
          .map((n) => [n.id, { kind: n.data.def.instrument ?? "graph", params: n.data.params, ...absolute(n, nodes) }]),
      ),
    [nodes],
  );
  const errors = state.logs.filter((l) => l.level === "error").length;
  const open = (p: Page) => {
    setPage(p);
    setMode("navigator");
  };
  const at = PAGES.findIndex((p) => p.id === page);
  const panel = (p: Page) => ({ page: p, mode, current: page === p, at, onOpen: () => open(p), drawer: drawerOf(p) });
  // Closed, the Board's head still shows the latest of its first few number readouts.
  const peek = Object.keys(monitors)
    .filter((id) => typeof state.latest[id] === "number")
    .slice(0, 3)
    .map((id) => (
      <span key={id}>
        {id} <b>{+(state.latest[id] as number).toPrecision(4)}</b>
      </span>
    ));
  const drawerOf = (p: Page): Drawer | undefined =>
    mode === "commander" && p === "dashboard"
      ? { ...boardDrawer, peek: peek.length ? peek : undefined }
      : mode === "lookout" && p === "station"
        ? stationDrawer
        : mode === "lookout" && p === "flow"
          ? flowDrawer
          : undefined;
  const hasMissingBlocks = nodes.some((n) => isBlock(n) && n.data.missing);
  const hasMissingToolkits = state.toolkits.some((t) => t.required && !t.installed);
  const canRun = state.connection === "open" && !running && !hasMissingBlocks && !hasMissingToolkits;
  const whyNotRun = running
    ? "A run is in progress"
    : state.connection !== "open"
      ? "Not connected to a backend"
      : hasMissingToolkits
        ? "Required toolkits are not installed"
        : hasMissingBlocks
          ? "Flow contains blocks with missing types"
          : "";
  const run = () => {
    if (hasMissingToolkits) {
      setMissingDialog(state.toolkits.filter((t) => t.required && !t.installed));
      return;
    }
    if (!canRun) return;
    if (mode === "commander") boardDrawer.setOpen(true); // a run is watched on the Board
    send({
      type: "graph.run",
      graph: toGraph(nodes, edges),
      ...(station ? { station } : {}),
      ...(Object.keys(runSettings).length ? { settings: runSettings } : {}),
      variables,
    });
  };

  const status = state.run?.status;
  const durationVal = Number(runSettings.duration ?? state.runDefaults.duration);
  const isInfinite = durationVal <= 0;
  const simTime = state.run?.simTime ?? (isInfinite ? 0 : (state.run ? state.run.progress * durationVal : 0));
  const speed = Number(runSettings.speed ?? state.runDefaults.speed ?? 1);
  const wallElapsed = state.run?.startedAt ? (Date.now() - state.run.startedAt) / 1000 : 0;
  const expectedWall = speed > 0 ? simTime / speed : 0;
  const delaySeconds = speed > 0 && wallElapsed > 1.2 && simTime > 0 ? wallElapsed - expectedWall : 0;
  const isDelayed = delaySeconds > 0.2;

  const elapsed = state.run
    ? isInfinite
      ? `${simTime.toFixed(1)} s`
      : `${(state.run.progress * durationVal).toFixed(1)} / ${durationVal} s`
    : "";
  const delayNotice = isDelayed ? <span className="run-delay" title="Real-time pacing is falling behind the wall clock"> ⚠ delay +{delaySeconds.toFixed(1)}s</span> : null;

  // ---- Projects (#82): the whole session as one file ----
  // A project is the Flow, the Station, the variables, the Dashboard layout and the run settings. `projectKey` is
  // that, as text, a moment after the last change; it is "unsaved" while it differs from `baseline`, which is
  // what the backend started with, or what was last saved or opened.
  const [projectTitle, setProjectTitle] = useState("Untitled");
  const fileHandle = useRef<FileHandle | null>(null); // where Save writes, once the user chose a file (Chromium)
  const [notice, setNotice] = useState<NoticeSpec | null>(null);
  const [toast, setToast] = useState<{ text: string; undo: () => void } | null>(null);
  const [recent, setRecent] = useState<Stored[]>(loadRecent);
  const [lastSession, setLastSession] = useState<Stored | undefined>(undefined); // unsaved work found at load
  const [restoreChecked, setRestoreChecked] = useState(false);
  const [baseline, setBaseline] = useState<string | null>(null);
  const [projectKey, setProjectKey] = useState("");
  const snapshot = (): Project => buildProject({ backend: state.engine, flow: toFlow(nodes, edges), station, variables, dashboard: dashSaved, run: runSettings });
  const latestProject = useRef<Project | null>(null);
  const dirty = baseline !== null && projectKey !== "" && projectKey !== baseline;
  const live = useRef({ snapshot, projectTitle, baseline, dirty, state });
  live.current = { snapshot, projectTitle, baseline, dirty, state };

  useEffect(() => {
    if (!state.engine) return;
    const t = window.setTimeout(() => {
      const p = snapshot();
      latestProject.current = p;
      setProjectKey(JSON.stringify(p));
    }, 250);
    return () => window.clearTimeout(t);
  }, [nodes, edges, station, variables, dashSaved, runSettings, state.engine]); // eslint-disable-line react-hooks/exhaustive-deps -- `snapshot` reads exactly these
  // Another backend: a new, unnamed project.
  useEffect(() => {
    setProjectTitle("Untitled");
    fileHandle.current = null;
    setBaseline(null);
    setRestoreChecked(false);
    setLastSession(undefined);
  }, [state.engine]);
  // The backend loaded its starter again: with nothing changed, clean is whatever that settles to.
  useEffect(() => {
    if (!live.current.dirty) setBaseline(null);
  }, [state.loaded]);
  useEffect(() => {
    if (baseline !== null || projectKey === "") return;
    const t = window.setTimeout(() => setBaseline(projectKey), 500);
    return () => window.clearTimeout(t);
  }, [projectKey, baseline]);
  useEffect(() => {
    document.title = `${dirty ? "● " : ""}${projectTitle} · Paperboat`;
  }, [projectTitle, dirty]);

  /** Put a project in place of what is open. `rebase`: what counts as unsaved against (null: what is loaded). */
  const applyProject = (p: Project, name: string, rebase: string | null = null) => {
    const g = fromGraph(p.flow, state.blocks);
    setNodes(g.nodes);
    setEdges(g.edges);
    setFlowLevel(null);
    editStation(p.station ?? state.station ?? { objects: [] });
    varsHistory.current = { past: [], future: [] };
    if (p.variables) keepVars(p.variables);
    else {
      setMine(undefined);
      saveMine(engineKey, undefined);
    }
    saveDash(p.dashboard);
    setRunSettings(p.run ?? {});
    setProjectTitle(name);
    setBaseline(rebase);
    requestAnimationFrame(() => fitView());
  };
  const remember = (p: Project, name: string) =>
    setRecent((prev) => {
      const next = addRecent(prev, { name, savedAt: Date.now(), project: p });
      saveRecent(next);
      return next;
    });
  const fail = (title: string, e: unknown) => {
    const msg = e instanceof Error ? e.message : String(e);
    log("error", `${title}: ${msg}`);
    setNotice({ title, text: [msg], actions: [{ label: "OK", primary: true }] });
  };

  /** Open a project, after a word if it comes from another backend or replaces unsaved changes; one Undo brings back what was open. */
  const openProject = (p: Project, name: string) => {
    const warnings: string[] = [];
    if (p.backend !== state.engine) warnings.push(`This project was made on “${p.backend}” and you are connected to “${state.engine}”. Blocks this backend lacks stay as lost blocks, with their params and wires.`);
    if (live.current.dirty) warnings.push("The open project has unsaved changes. Opening replaces them; Undo brings them back.");
    const go = () => {
      const { snapshot: before, projectTitle: beforeTitle, baseline: beforeBase } = live.current;
      const was = before();
      const handle = fileHandle.current;
      applyProject(p, name);
      fileHandle.current = null;
      remember(p, name);
      setToast({
        text: `Opened “${name}”`,
        undo: () => {
          applyProject(was, beforeTitle, beforeBase);
          fileHandle.current = handle;
        },
      });
    };
    if (warnings.length === 0) go();
    else setNotice({ title: "Open this project?", text: warnings, actions: [{ label: "Open", primary: true, run: go }, { label: "Cancel" }] });
  };
  const openText = (text: string, file: string) => {
    const r = parseProject(text);
    if ("error" in r) {
      log("error", `${file}: ${r.error}`);
      setNotice({ title: "Can't open this file", text: [`${file}: ${r.error}`, "Nothing was changed."], actions: [{ label: "OK", primary: true }] });
    } else openProject(r.project, projectName(file));
  };
  const openFile = async () => {
    try {
      const f = await pickText();
      if (f) openText(f.text, f.name);
    } catch (e) {
      fail("Can't open the file", e);
    }
  };
  const saveProject = async (as: boolean) => {
    const { snapshot: take, projectTitle: title } = live.current;
    const p = take();
    try {
      const r = await saveText(stringify(p), projectFileName(title === "Untitled" ? state.engine || "project" : title), as ? null : fileHandle.current);
      if (!r) return; // cancelled
      fileHandle.current = r.handle ?? null;
      const name = projectName(r.name);
      setProjectTitle(name);
      setBaseline(JSON.stringify(p));
      remember(p, name);
      clearAutosave(state.engine);
      setLastSession(undefined);
    } catch (e) {
      fail("Can't save", e);
    }
  };
  // Dropping a project file on the window opens it (the app's own cards are not files).
  const openTextNow = useRef(openText);
  openTextNow.current = openText;
  useEffect(() => {
    const over = (e: DragEvent) => draggingFiles(e) && e.preventDefault();
    const drop = (e: DragEvent) => {
      if (!draggingFiles(e)) return;
      e.preventDefault();
      droppedText(e).then((f) => f && openTextNow.current(f.text, f.name), (err) => fail("Can't open the file", err));
    };
    window.addEventListener("dragover", over);
    window.addEventListener("drop", drop);
    return () => {
      window.removeEventListener("dragover", over);
      window.removeEventListener("drop", drop);
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps -- `fail` only sets state
  // Unsaved work is kept as it happens, and offered back after a reload.
  const restore = (s: Stored) => {
    applyProject(s.project, s.name, live.current.baseline);
    fileHandle.current = null;
    setLastSession(undefined);
  };
  useEffect(() => {
    if (baseline === null || restoreChecked || !state.engine) return;
    setRestoreChecked(true);
    const s = loadAutosave(state.engine);
    if (!s || JSON.stringify(s.project) === baseline) return clearAutosave(state.engine);
    setLastSession(s);
    setNotice({
      title: "Restore your last session?",
      text: [`“${s.name}”, ${ago(s.savedAt, Date.now())}, had changes that weren't saved.`],
      actions: [
        { label: "Restore", primary: true, run: () => restore(s) },
        { label: "Discard", run: () => (clearAutosave(s.project.backend), setLastSession(undefined)) },
        { label: "Not now" },
      ],
    });
  }, [baseline, restoreChecked, state.engine]); // eslint-disable-line react-hooks/exhaustive-deps -- once per backend
  useEffect(() => {
    if (!restoreChecked || !state.engine || baseline === null || !latestProject.current) return;
    if (dirty) saveAutosave(state.engine, { name: projectTitle, savedAt: Date.now(), project: latestProject.current });
    else if (!lastSession) clearAutosave(state.engine);
  }, [projectKey, dirty, restoreChecked]); // eslint-disable-line react-hooks/exhaustive-deps -- when the project changes

  // ---- Missing toolkit prompt and post-install restart prompt (#118) ----
  const promptedToolkits = useRef<string>("");
  const prevBusyToolkits = useRef<Map<string, string>>(new Map());
  const [missingDialog, setMissingDialog] = useState<ToolkitStatus[] | null>(null);
  const installQueue = useRef<{ name: string; version: string }[]>([]);
  const autoRestart = useRef(false);

  const restartBackend = useCallback(() => {
    log("info", "restarting backend…");
    send({ type: "backend.restart" });
  }, [log, send]);
  const installToolkit = useCallback(
    (name: string, version?: string) => {
      log("info", `requesting install of toolkit "${name}"${version ? ` (${version})` : ""}…`);
      send({ type: "toolkit.install", name, version });
    },
    [log, send],
  );
  const toolkitVersions = useCallback(
    (name: string) => {
      send({ type: "toolkit.versions", name });
    },
    [send],
  );
  const uninstallToolkit = useCallback(
    (name: string) => {
      log("info", `requesting uninstall of toolkit "${name}"…`);
      send({ type: "toolkit.uninstall", name });
    },
    [log, send],
  );
  useEffect(() => {
    if (state.connection !== "open") return;
    const missingTypes = nodes.filter(isBlock).filter((n) => n.data.missing).map((n) => n.data.def.type);
    const missing = missingRequiredToolkits(state.toolkits, missingTypes);
    if (missing.length === 0) {
      if (missingDialog) setMissingDialog(null);
      return;
    }
    const key = `${state.engine}:${missing.map((t) => t.name).sort().join(",")}`;
    if (promptedToolkits.current === key) return;
    promptedToolkits.current = key;
    for (const t of missing) {
      if (!t.versions || t.versions.length === 0) {
        toolkitVersions(t.name);
      }
    }
    setMissingDialog(missing);
  }, [state.connection, state.engine, state.toolkits, nodes, toolkitVersions, missingDialog]);

  useEffect(() => {
    const prev = prevBusyToolkits.current;
    const finished: string[] = [];
    const failed: string[] = [];
    for (const t of state.toolkits) {
      const was = prev.get(t.name);
      if ((was === "installing" || was === "updating") && !t.busy) {
        if (t.installed) {
          finished.push(t.name);
        } else {
          failed.push(t.name);
        }
      }
    }
    const next = new Map<string, string>();
    for (const t of state.toolkits) if (t.busy) next.set(t.name, t.busy);
    prevBusyToolkits.current = next;

    if (failed.length > 0) {
      installQueue.current = [];
      autoRestart.current = false;
      setNotice({
        title: "Toolkit installation failed",
        text: [`Installing ${failed.join(", ")} failed. Check the Console for details.`],
        actions: [
          { label: "Open Console", primary: true, run: () => setConsoleShown(true) },
          { label: "Dismiss" },
        ],
      });
      return;
    }

    if (finished.length > 0) {
      if (installQueue.current.length > 0) {
        const nextItem = installQueue.current.shift()!;
        installToolkit(nextItem.name, nextItem.version);
        return;
      }
      if (autoRestart.current) {
        autoRestart.current = false;
        restartBackend();
      } else {
        const names = finished.join(", ");
        setNotice({
          title: `Toolkit ${names} ready`,
          text: [`Restart the backend so it loads the updated toolkit (${names}).`],
          actions: [
            { label: "Restart backend", primary: true, run: restartBackend },
            { label: "Later" },
          ],
        });
      }
    }
  }, [state.toolkits, restartBackend, installToolkit]);

  // ---- The commands (#78): what the menus, the command search and the shortcuts all run ----
  // Only what the app already does; backends' commands come with their issue (#85).
  const flowInView = mode === "commander" || (mode === "lookout" ? flowDrawer.open : page === "flow");
  const stationInView = view === "graph" && (mode === "commander" || (mode === "lookout" ? stationDrawer.open : page === "station"));
  const arrangeVisible = () => {
    if (flowInView) arrangeCanvas("flow");
    if (stationInView) arrangeCanvas("station");
  };
  /** An Edit action on the canvas: on while the Flow is in view and the canvas allows it. */
  const onCanvas = (can: (c: FlowControls["can"]) => boolean) => () => flowInView && !!flow.current && can(flow.current.can);
  const canvasWhy = (why: string) => (flowInView ? why : "Open the Flow page first");
  const commands: Command[] = [
    { id: "file.open", label: "Open…", menu: "file", group: "project", shortcut: "Ctrl+O", chord: { key: "o", ctrl: true }, enabled: state.connection === "open", why: "Not connected to a backend", run: () => void openFile() },
    { id: "file.save", label: "Save", menu: "file", group: "project", shortcut: "Ctrl+S", chord: { key: "s", ctrl: true }, enabled: !!state.engine, why: "Not connected to a backend yet", run: () => void saveProject(false) },
    { id: "file.saveAs", label: "Save as…", menu: "file", group: "project", shortcut: "Ctrl+Shift+S", chord: { key: "s", ctrl: true, shift: true }, enabled: !!state.engine, why: "Not connected to a backend yet", run: () => void saveProject(true) },
    { id: "file.restore", label: "Restore last session", menu: "file", group: "session", enabled: !!lastSession, why: "No unsaved session to restore", run: () => lastSession && restore(lastSession) },
    ...recent.map((s): Command => ({ id: `file.recent.${s.name}`, label: s.name, menu: "file", group: "recent", meta: ago(s.savedAt, Date.now()), enabled: state.connection === "open", why: "Not connected to a backend", run: () => openProject(s.project, s.name) })),
    { id: "file.backend", label: "Backend…", menu: "file", group: "backend", run: () => setBackendOpen(true) },
    { id: "file.restartBackend", label: "Restart backend", menu: "file", group: "backend", enabled: state.connection === "open", why: "Not connected to a backend", run: restartBackend },

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
    { id: "view.mode.lookout", label: "Lookout", menu: "view", group: "mode", checked: mode === "lookout", run: () => setMode("lookout") },
    { id: "view.mode.switch", label: "Switch view mode", menu: "view", group: "mode", shortcut: "Alt+M", chord: { key: "m", alt: true }, run: () => setMode(MODES[(MODES.indexOf(mode) + 1) % MODES.length]) },
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
      id: "view.arrangeBlocks",
      label: "Arrange Blocks",
      menu: "view",
      group: "canvas",
      enabled: () => !running && ((flowInView && !!flow.current?.can.arrange) || (stationInView && !!stationCanvas.current?.can.arrange)),
      why: running ? "Stop the run to arrange blocks" : flowInView || stationInView ? "There are no blocks to arrange" : "Open the Flow or Station graph first",
      run: arrangeVisible,
    },
    {
      id: "view.arrange",
      label: "Arrange Board",
      menu: "view",
      group: "windows",
      checked: arrange,
      run: () => {
        if (!arrange && mode === "navigator") setPage("dashboard");
        setArrange(!arrange);
      },
    },

    { id: "run.run", label: "Run", menu: "run", group: "run", enabled: canRun, why: whyNotRun || undefined, run },
    { id: "run.cancel", label: "Cancel", menu: "run", group: "run", enabled: running, why: "No run in progress", run: () => send({ type: "run.cancel" }) },
  ];
  useShortcuts(commands);

  return (
    <div className={`app mode-${mode}`}>
      <header className="bar">
        <Wordmark running={running} />
        <MenuBar commands={commands} />
        <ProjectLabel name={projectTitle} dirty={dirty} />
        <span className="spacer" />
        <ToolkitsButton
          toolkits={state.toolkits}
          connected={state.connection === "open"}
          onInstall={installToolkit}
          onUninstall={uninstallToolkit}
          onRestart={restartBackend}
          onVersions={toolkitVersions}
        />
        <button
          className={`key small${errors > 0 ? " err" : ""}${consoleShown ? " on" : ""}`}
          aria-pressed={consoleShown}
          title="Open the Console"
          onClick={() => setConsoleShown(!consoleShown)}
        >
          {errors > 0 ? count(errors, "error") : "Console"}
        </button>
        <Switch
          label="View"
          value={mode}
          onChange={setMode}
          options={[
            { value: "navigator", label: "navigator", hint: "One page at a time (Alt+M)" },
            { value: "commander", label: "commander", hint: "Station and Flow, the Board under them (Alt+M)" },
            { value: "lookout", label: "lookout", hint: "The Board, Station and Flow at its sides (Alt+M)" },
          ]}
        />
        <button className={`chip conn-${state.connection}`} title="Which backend: File > Backend…" onClick={() => setBackendOpen(true)}>
          <i aria-hidden="true" />
          {state.connection === "open" ? state.engine : state.connection}
        </button>
      </header>
      <Rail page={page} onPage={setPage} hidden={mode !== "navigator"} />
      <VarsContext.Provider value={varsApi}>
      <main className="pages">
        <div className="page-stack">
        <Panel {...panel("station")} actions={<>{gridKey}{arrangeKey("station")}{addKey("station")}</>} info={station ? `${count(station.objects.length, "object")} · ${view === "scene" ? state.view.mode : view}` : view}>
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
              <StationGraph objects={state.objects} station={station ?? { objects: [] }} onChange={editStation} locked={running} controls={stationCanvas} />
            ) : (
              <Suspense fallback={null}>
                <SceneView key={`${theme}:${JSON.stringify(state.view)}`} view={state.view} grid={gridOn} entities={state.entities} />
              </Suspense>
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
        <Panel {...panel("flow")} actions={<>{arrangeKey("flow")}{addKey("flow")}</>} info={count(nodes.filter(isBlock).length, "block")}>
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
              locked={running}
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
          <Dashboard state={state} monitors={monitors} arrange={arrange} onBlock={showBlock} saved={dashSaved} onSave={saveDash} url={url} subscribe={subscribe} send={send} gridOn={gridOn} />
        </Panel>
        </div>
        <VariablesSheet
          sheetOpen={varsShown === "open"}
          onShowSheet={() => setVarsShown("open")}
          open={openVar}
          onOpen={setOpenVar}
          onClose={() => setVarsShown("closed")}
          onShow={showUse}
          onUndo={undoVars}
          toml={() => toToml(variables)}
        />
        <Dock
          canRun={canRun}
          whyNot={whyNotRun}
          running={running}
          status={state.run ? status : undefined}
          statusText={status === "running" ? <>running {elapsed}{delayNotice}</> : status}
          defaults={state.runDefaults}
          settings={runSettings}
          onSettings={setRunSettings}
          onRun={run}
          onCancel={() => send({ type: "run.cancel" })}
        />
      </main>
      </VarsContext.Provider>
      {missingDialog && (
        <MissingToolkitsDialog
          missing={missingDialog.map((m) => state.toolkits.find((t) => t.name === m.name) ?? m)}
          onInstall={(selections) => {
            setMissingDialog(null);
            setConsoleShown(true);
            autoRestart.current = true;
            if (selections.length > 0) {
              const [first, ...rest] = selections;
              installQueue.current = rest;
              installToolkit(first.name, first.version);
            }
          }}
          onCancel={() => setMissingDialog(null)}
        />
      )}
      {notice && <Notice key={notice.title + notice.text.join()} spec={notice} onClose={() => setNotice(null)} />}
      {toast && <Toast text={toast.text} undo="Undo" onUndo={toast.undo} onClose={() => setToast(null)} />}
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
