import { useEdgesState, useNodesState, useReactFlow } from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useEngine } from "./engine";
import { BrowserWindow, DRAG_TYPE, dragging, type Item } from "./Browser";
import { FlowEditor, fromGraph, isBlock, portsOf, resolveBlocks, RunStates, syncLinked, toGraph, withBlock, type FlowNode, type Wire } from "./FlowEditor";
import { absolute, isFrame } from "./groups";
import type { RunSettings, Station } from "./protocol";
import { Console } from "./Console";
import { Dashboard } from "./Dashboard";
import { PAGES, Panel, Rail, Switch, Wordmark, type Mode, type Page, type Theme } from "./Shell";
import { SceneView } from "./SceneView";
import { StationGraph } from "./StationGraph";
import { StationPanel, withObject } from "./StationPanel";
import { RunOptions } from "./RunOptions";
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
  const [page, setPage] = remembered<Page>("paperboat.page", "flow", PAGE_IDS);
  const [themeChoice, setThemeChoice] = remembered<Theme>("paperboat.theme", "auto", ["auto", "light", "dark"]);
  const theme = useTheme(themeChoice);
  const [url, setUrl] = useState(DEFAULT_URL);
  const [draftUrl, setDraftUrl] = useState(DEFAULT_URL);
  const { state, send, subscribe } = useEngine(url);
  const [nodes, setNodes, onNodesChange] = useNodesState<FlowNode>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Wire>([]);
  const { fitView, screenToFlowPosition } = useReactFlow();
  const [station, setStation] = useState<Station | null>(null);
  const [runSettings, setRunSettings] = useState<RunSettings>({});
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
  useUndo(nodes, edges, setNodes, setEdges, state.loaded);

  // A page that changes size (another mode, or brought into view) fits its flow again.
  useEffect(() => {
    if (mode === "navigator" && page !== "flow") return;
    const t = window.setTimeout(() => fitView({ duration: 200 }), 60);
    return () => window.clearTimeout(t);
  }, [mode, page, fitView]);

  // Alt+1–4 opens a page (Navigator); Alt+M switches the view mode.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.altKey || e.ctrlKey || e.metaKey) return;
      const n = Number(e.key);
      if (n >= 1 && n <= PAGES.length) {
        e.preventDefault();
        setPage(PAGES[n - 1].id);
        setMode("navigator");
      } else if (e.key.toLowerCase() === "m") {
        e.preventDefault();
        setMode(mode === "navigator" ? "commander" : "navigator");
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });

  // Forward the keyboard to the backend (e.g. a Key block). Typing in a field is not input.
  useEffect(() => {
    const forward = (down: boolean) => (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.repeat || e.defaultPrevented) return; // taken by the app (quick add's /)
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

  // A Console line names a block: select it and bring it into view.
  const showBlock = (id: string) => {
    const node = nodes.find((n) => n.id === id);
    if (!node) return;
    setPage("flow");
    // Open the group it's in, if that's collapsed.
    setNodes((ns) =>
      ns.map((n) =>
        n.id === node.parentId && isFrame(n) ? { ...n, data: { ...n.data, collapsed: false } } : { ...n, selected: n.id === id },
      ),
    );
    fitView({ nodes: [{ id }], duration: 300, maxZoom: 1.2 });
  };

  const view = state.stationView.view;

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
    setNodes((ns) => withBlock(ns, item.def, { x: at.x - 78 + (ns.length % 5) * 18, y: at.y - 30 + (ns.length % 5) * 18 })[0]);
    if (mode === "navigator") setPage("flow");
  };
  const addKey = (which: "flow" | "station") => (
    <button className="key small add" disabled={which === "station" && (running || state.objects.length === 0)} onClick={() => setBrowser(browser === which ? null : which)}>
      + Add
    </button>
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
    });

  const status = state.run?.status;
  const elapsed = state.run ? `${(state.run.progress * (Number(runSettings.duration ?? state.runDefaults.duration) || 0)).toFixed(1)} / ${runSettings.duration ?? state.runDefaults.duration ?? "?"} s` : "";

  return (
    <div className={`app mode-${mode}`}>
      <header className="bar">
        <Wordmark />
        <form
          className="backend"
          onSubmit={(e) => {
            e.preventDefault();
            setUrl(draftUrl);
          }}
        >
          <span className="lbl">Backend</span>
          <span className="well field">
            <input aria-label="Backend address" value={draftUrl} onChange={(e) => setDraftUrl(e.target.value)} size={20} />
            <span className={`conn conn-${state.connection}`}>
              {state.connection === "open" ? state.engine : state.connection}
            </span>
          </span>
          <button className="key small">Connect</button>
        </form>
        <div className="run-controls">
          <button className="key run" disabled={state.connection !== "open" || running} onClick={run}>
            ▶ Run
          </button>
          <RunOptions defaults={state.runDefaults} value={runSettings} onChange={setRunSettings} />
          <button className="key" disabled={!running} onClick={() => send({ type: "run.cancel" })}>
            ■ Cancel
          </button>
          {state.run && (
            <span className={`run-status status-${status}`}>
              {status === "running" ? `running ${elapsed}` : status}
            </span>
          )}
        </div>
        <Switch
          label="View"
          value={mode}
          onChange={setMode}
          options={[
            { value: "navigator", label: "navigator", hint: "One page at a time (Alt+M)" },
            { value: "commander", label: "commander", hint: "Every page at once (Alt+M)" },
          ]}
        />
        <Switch
          label="Theme"
          value={themeChoice}
          onChange={setThemeChoice}
          options={[
            { value: "auto", label: "auto", hint: "As the system" },
            { value: "light", label: "light" },
            { value: "dark", label: "dark" },
          ]}
        />
      </header>
      {mode === "navigator" && <Rail page={page} onPage={setPage} errors={errors} />}
      <main className="pages">
        <Panel {...panel("station")} actions={addKey("station")} info={station ? `${count(station.objects.length, "object")} · ${view === "scene" ? state.view.mode : view}` : view}>
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
              <SceneView key={`${theme}:${JSON.stringify(state.view)}`} view={state.view} entities={state.entities} />
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
            />
          </RunStates.Provider>
        </Panel>
        <Panel {...panel("dashboard")} info={count(Object.keys(state.latest).length, "instrument")}>
          <Dashboard
            state={state}
            monitors={Object.fromEntries(
              nodes.filter(isBlock).map((n) => [n.id, { kind: n.data.def.instrument ?? "graph", ...absolute(n, nodes) }]),
            )}
          />
        </Panel>
        <Panel {...panel("console")} info={`${count(state.logs.length, "line")}${errors ? ` · ${count(errors, "error")}` : ""}`}>
          <Console logs={state.logs} onBlock={showBlock} />
        </Panel>
      </main>
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
