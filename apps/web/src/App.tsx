import { useEdgesState, useNodesState, useReactFlow } from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useEngine } from "./engine";
import { BrowserWindow, type Item } from "./Browser";
import { FlowEditor, type FlowControls } from "./FlowEditor";
import { RunStates } from "./flowContexts";
import { isBlock, type FlowNode, type Wire } from "./flowModel";
import { fromGraph, toGraph, withBlock } from "./flowGraph";
import { isFrame, levelOf } from "./groups";
import type { DashboardLayout, FlowType, Project, RunSettings } from "./protocol";
import { inferFlow, offeredFlows, sheetFlow } from "./flowTypes";
import { SheetFlow } from "./SheetFlow";
import { Notice } from "./ProjectUI";
import { VariablesSheet } from "./Variables";
import { VarsContext } from "./VarsContext";
import { toToml, type Use } from "./bindings";
import { ConsoleWindow } from "@paperboat/shared/components";
import { Dashboard } from "./Dashboard";
import { loadSaved, save as saveLayout } from "./dashboardLayout";
import { remembered, useDrawer } from "./remembered";
import { PAGE_IDS, useUiLock } from "./uiLock";
import { useVariables, varsApiFor } from "./variablesState";
import { useCanvasSync } from "./canvasSync";
import { useBoardWiring } from "./boardWiring";
import { dockSettings, runBlocker, runClock, speedChange } from "./runStatus";
import { useProjectSession } from "./projectSession";
import { useToolkitInstall } from "./toolkitInstall";
import { menuCommands, solverChoice } from "./menuCommands";
import { solverShort } from "./solvers";
import { count } from "./words";
import { AppHeader } from "./AppHeader";
import { StationView } from "./StationView";
import { useBackendKeys } from "./backendKeys";
import { MODES, PAGES, Panel, Rail, useTheme, type Drawer, type Mode, type Page, type Theme } from "./Shell";
import type { StationControls } from "./StationGraph";
import { withObject } from "./StationPanel";
import { BackendWindow, Dock } from "./Dock";
import { CommandSearch } from "./Menus";
import { useShortcuts } from "./commands";
import { MissingToolkitsDialog } from "./Toolkits";
import { useUndo } from "./undo";
import { LockDialog } from "./LockDialog";
import { SolverDialog } from "./SolverDialog";

/** The glass dock floats over the canvas's bottom, and the minimap over its corner: fit leaves room for both. */
const FIT_PADDING = { top: "16px", right: "16px", bottom: "96px", left: "16px" } as const;
const FIT_PADDING_MINIMAP = { ...FIT_PADDING, bottom: "220px" } as const;

const DEFAULT_URL =
  new URLSearchParams(location.search).get("backend") ??
  import.meta.env.VITE_ENGINE_URL ??
  "ws://localhost:8765";

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
  const { projectLock, setProjectLock, unlocked, setUnlocked, lockModalOpen, setLockModalOpen, locked, lockedArrange, allowedPages, allowedModes } = useUiLock(arrange, setArrange, page, setPage, mode, setMode);
  const [solverOpen, setSolverOpen] = useState(false);

  const [url, setUrl] = useState(DEFAULT_URL);
  const [searchOpen, setSearchOpen] = useState(false); // the command search (Ctrl+K)
  const [backendOpen, setBackendOpen] = useState(false); // File > Backend…
  const flow = useRef<FlowControls | null>(null); // the canvas's actions, for the menus
  const stationCanvas = useRef<StationControls | null>(null);
  const { state, send, subscribe, log } = useEngine(url);
  const [nodes, setNodes, onNodesChange] = useNodesState<FlowNode>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Wire>([]);
  const { fitView, screenToFlowPosition } = useReactFlow();
  const [runSettings, setRunSettings] = useState<RunSettings>({});
  // Variables (#36): the backend's defaults, then the user's own, kept in the project (#34).
  const { variables, keepVars, setVariables, varsHistory, undoVars } = useVariables(state, send, subscribe);
  const engineKey = state.engine || "default";
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
  useEffect(() => setRunSettings({}), [state.engine]); // another backend: its own defaults

  // The Flow sheet's flow type, if one was chosen (#9); null: taken from its blocks.
  const [flowChoice, setFlowChoice] = useState<FlowType | null>(null);
  // Fit the Flow leaving room for the glass dock and (on the Flow page) the minimap.
  const fitFlow = useCallback(() => {
    fitView({ padding: mode === "navigator" && page === "flow" ? FIT_PADDING_MINIMAP : FIT_PADDING });
  }, [fitView, mode, page]);
  // The canvas and the station follow the backend, keeping what the user changed since it loaded them.
  const { station, editStation } = useCanvasSync(state, send, nodes, edges, setNodes, setEdges, fitFlow, setFlowChoice);

  // Ctrl+Z / Ctrl+Shift+Z on the Flow canvas; a new graph from the backend starts afresh.
  const history = useUndo(nodes, edges, setNodes, setEdges, state.loaded);

  // A page that changes size (another mode, or brought into view) fits its flow again.
  useEffect(() => {
    if (mode === "navigator" && page !== "flow") return;
    // After the rail has stepped aside or the column has slid (both about half a second).
    const t = window.setTimeout(() => fitView({ duration: 200, padding: mode === "navigator" && page === "flow" ? FIT_PADDING_MINIMAP : FIT_PADDING }), 520);
    return () => window.clearTimeout(t);
  }, [mode, page, boardDrawer.open, flowDrawer.open, fitView]);

  // (Alt+1–3, Alt+M, Alt+V and Ctrl+K are commands now: see `commands` below.)

  // Selecting a variable unselects the blocks and wires, so their actions don't cover its own (#79).
  useEffect(() => {
    if (!openVar) return;
    setNodes((ns) => (ns.some((n) => n.selected) ? ns.map((n) => (n.selected ? { ...n, selected: false } : n)) : ns));
    setEdges((es) => (es.some((e) => e.selected) ? es.map((e) => (e.selected ? { ...e, selected: false } : e)) : es));
  }, [openVar, setNodes, setEdges]);

  // Forward the keyboard to the backend (e.g. a Key block). Typing in a field is not input.
  useBackendKeys(send);

  const running = state.run?.status === "running";
  const runStates = useMemo(
    () => ({ active: new Set(state.active), failed: new Set(state.failed), queued: new Set(state.queued), cached: new Set(state.cached), progress: state.progress }),
    [state.active, state.failed, state.queued, state.cached, state.progress],
  );

  // The subsystem or story open on the Flow canvas (#68); null: the top level.
  const [flowLevel, setFlowLevel] = useState<string | null>(null);
  // The open sheet's flow type (#9): what the palette offers on it.
  const offered = useMemo(() => offeredFlows(state.flows, state.blocks), [state.flows, state.blocks]);
  const sheet = sheetFlow(flowChoice ?? inferFlow(nodes), flowLevel, nodes);

  // A Console line names a block: open the level it is on, select it and bring it into view.
  const showBlock = (id: string) => {
    const node = nodes.find((n) => n.id === id);
    if (!node) return;
    if (mode === "lookout") flowDrawer.setOpen(true);
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
  const varsApi = varsApiFor({ state, variables, setVariables, varsHistory, running, nodes, setNodes, station, editStation, dashLayout, saveDash, setOpenVar, setVarsShown });
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
  const [cameraProjection, setCameraProjection] = useState<"persp" | "iso">("persp");
  const gridOn = gridChoice === "auto" ? (state.view.mode === "3d" ? state.view.grid !== false : true) : gridChoice === "on";
  const projectionKey =
    view === "scene" && state.view.mode === "3d" ? (
      <button
        className={`key small${cameraProjection === "iso" ? " on" : ""}`}
        aria-pressed={cameraProjection === "iso"}
        title={cameraProjection === "iso" ? "Isometric (parallel) view: click for Perspective" : "Perspective view: click for Isometric (parallel)"}
        onClick={() => setCameraProjection((p) => (p === "iso" ? "persp" : "iso"))}
      >
        {cameraProjection === "iso" ? "Iso" : "Persp"}
      </button>
    ) : null;
  const gridKey =
    view === "scene" && state.view.mode === "3d" ? (
      <button className={`key small${gridOn ? " on" : ""}`} aria-pressed={gridOn} title="Show or hide the floor grid" onClick={() => setGridChoice(gridOn ? "off" : "on")}>
        Grid
      </button>
    ) : null;
  // A key that can't run now stays and says why in its tooltip (DESIGN.md).
  const addWhy = (which: "flow" | "station") =>
    which === "flow"
      ? "Add a block"
      : running
        ? "Stop the run to add station objects"
        : state.objects.length === 0
          ? "This backend has no station objects to add"
          : "Add a station object";
  const addKey = (which: "flow" | "station") => (
    <button
      className="key small add"
      title={addWhy(which)}
      disabled={which === "station" && (running || state.objects.length === 0)}
      onClick={() => setBrowser(browser === which ? null : which)}
    >
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
  // The Board's instruments are the monitor blocks; + tap wires an output into one.
  const { monitors, taps, tap } = useBoardWiring(nodes, edges, setNodes, setEdges, log);
  const errors = state.logs.filter((l) => l.level === "error").length;
  const open = (p: Page) => {
    setPage(p);
    setMode("navigator");
  };
  const at = PAGES.findIndex((p) => p.id === page);
  // The Flow is unmounted while Lookout's drawer is closed (React Flow needs a size to draw into).
  const flowShown = mode !== "lookout" || flowDrawer.open;
  // Its head counts the level in view, not the whole flow.
  const flowInfo = useMemo(() => {
    const here = nodes.filter((n) => isBlock(n) && levelOf(n.id, nodes) === flowLevel);
    if (!flowLevel) return count(here.length, "block");
    const group = nodes.find((n) => n.id === flowLevel);
    return `${count(here.length, "block")} in ${group && isFrame(group) ? group.data.label : "this level"}`;
  }, [nodes, flowLevel]);
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
  const whyNotRun = runBlocker(running, state.connection, hasMissingToolkits, hasMissingBlocks);
  const run = () => {
    if (hasMissingToolkits) {
      setMissingDialog(state.toolkits.filter((t) => t.required && !t.installed));
      return;
    }
    if (!canRun) return;
    if (mode === "commander") boardDrawer.setOpen(true); // a run is watched on the Board
    send({
      type: "graph.run",
      graph: toGraph(nodes, edges, flowChoice),
      ...(station ? { station } : {}),
      ...(Object.keys(runSettings).length ? { settings: runSettings } : {}),
      variables,
    });
  };

  const status = state.run?.status;
  const { elapsed, delaySeconds, isDelayed } = runClock(state.run, runSettings, state.runDefaults, Date.now());
  const delayNotice = isDelayed ? <span className="run-delay" title="Real-time pacing is falling behind the wall clock"> ⚠ delay +{delaySeconds.toFixed(1)}s</span> : null;

  // ---- Projects (#82): the whole session as one file ----
  /** Put what a project holds in place of what is open: the Flow, the Station, variables, Board, run settings and lock. */
  const loadProject = (p: Project) => {
    const g = fromGraph(p.flow, state.blocks);
    setNodes(g.nodes);
    setEdges(g.edges);
    setFlowChoice(p.flow.flow ?? null);
    setFlowLevel(null);
    editStation(p.station ?? state.station ?? { objects: [] });
    varsHistory.current = { past: [], future: [] };
    keepVars(p.variables);
    saveDash(p.dashboard);
    setRunSettings(p.run ?? {});
    const lock = p.ui?.lock ?? null;
    setProjectLock(lock);
    setUnlocked(!lock);
    if (lock) {
      if (lock.arrange) setArrange(false);
      if (lock.pages && lock.pages.length > 0 && !lock.pages.includes(page)) {
        setPage(lock.pages[0] as Page);
      }
      if (lock.modes && lock.modes.length > 0 && !lock.modes.includes(mode)) {
        setMode(lock.modes[0] as Mode);
      }
    }
    requestAnimationFrame(() => fitView({ padding: FIT_PADDING }));
  };
  const { projectTitle, dirty, notice, setNotice, undoOpen, recent, lastSession, restore, openProject, openFile, saveProject } = useProjectSession(
    state,
    subscribe,
    log,
    { nodes, edges, station, variables, dashSaved, runSettings, projectLock, flowChoice },
    loadProject,
  );

  // ---- Missing toolkit prompt and post-install restart prompt (#118) ----
  const { missingDialog, setMissingDialog, restartBackend, installToolkit, toolkitVersions, uninstallToolkit, installAll } = useToolkitInstall(state, send, log, nodes);

  // ---- The commands (#78): what the menus, the command search and the shortcuts all run ----
  const { usesSolvers, currentSolver, solvers } = solverChoice(state, nodes, runSettings);
  const commands = menuCommands({
    state, send, openFile, saveProject, lastSession, restore, recent, openProject, projectLock, unlocked, locked, lockedArrange, setLockModalOpen, setBackendOpen, restartBackend,
    history, undoOpen, flow, stationCanvas, setBrowser, running, variables, varsShown, setVarsShown,
    setSearchOpen, mode, setMode, allowedModes, page, setPage, open, stationDrawer, flowDrawer, view, themeChoice, setThemeChoice, consoleShown, setConsoleShown, errors,
    gridOn, setGridChoice, cameraProjection, setCameraProjection, arrangeCanvas, arrange, setArrange,
    canRun, whyNotRun, run, usesSolvers, currentSolver, solvers, setRunSettings, setSolverOpen,
  });
  useShortcuts(commands);

  return (
    <div className={`app mode-${mode}`}>
      <AppHeader
        state={state}
        running={running}
        commands={commands}
        solver={usesSolvers ? solverShort(currentSolver) : undefined}
        projectTitle={projectTitle}
        dirty={dirty}
        installToolkit={installToolkit}
        uninstallToolkit={uninstallToolkit}
        restartBackend={restartBackend}
        toolkitVersions={toolkitVersions}
        errors={errors}
        consoleShown={consoleShown}
        onConsole={() => setConsoleShown(!consoleShown)}
        mode={mode}
        setMode={setMode}
        allowedModes={allowedModes}
        projectLock={projectLock}
        locked={locked}
        onLock={() => setLockModalOpen(true)}
        onBackend={() => setBackendOpen(true)}
        themeChoice={themeChoice}
        setThemeChoice={setThemeChoice}
      />
      <Rail page={page} onPage={setPage} hidden={mode !== "navigator"} allowedPages={allowedPages} />
      <VarsContext.Provider value={varsApi}>
      <main className="pages">
        <div className="page-stack">
        <Panel {...panel("station")} actions={<>{projectionKey}{gridKey}{arrangeKey("station")}{addKey("station")}</>} info={station ? `${count(station.objects.length, "object")} · ${view === "scene" ? state.view.mode : view}` : view}>
          <StationView
            view={view}
            state={state}
            url={url}
            subscribe={subscribe}
            send={send}
            station={station}
            editStation={editStation}
            running={running}
            stationCanvas={stationCanvas}
            theme={theme}
            gridOn={gridOn}
            cameraProjection={cameraProjection}
            taken={taken}
            addObject={addObject}
          />
        </Panel>
        <Panel
          {...panel("flow")}
          actions={
            <>
              <SheetFlow offered={offered} flow={sheet.flow} story={sheet.story} chosen={flowChoice !== null} disabled={running} onChoose={setFlowChoice} />
              {arrangeKey("flow")}
              {addKey("flow")}
            </>
          }
          info={flowInfo}
        >
          {flowShown && (
          <RunStates.Provider value={runStates}>
            <FlowEditor
              blocks={state.blocks}
              connectors={state.connectors}
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
              sheetFlow={sheet.flow}
              fitPadding={mode === "navigator" && page === "flow" ? FIT_PADDING_MINIMAP : FIT_PADDING}
            />
          </RunStates.Provider>
          )}
        </Panel>
        <Panel
          {...panel("dashboard")}
          actions={
            !lockedArrange ? (
              <button className={`key small${arrange ? " on" : ""}`} aria-pressed={arrange} title="Move instruments and pick their sizes" onClick={() => setArrange(!arrange)}>
                {arrange ? "Done" : "Arrange"}
              </button>
            ) : undefined
          }
          info={count(Object.keys(monitors).length, "instrument")}
        >
          <Dashboard state={state} monitors={monitors} taps={taps} onTap={tap} arrange={arrange} onBlock={showBlock} saved={dashSaved} onSave={saveDash} url={url} subscribe={subscribe} send={send} gridOn={gridOn} />
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
          settings={dockSettings(state.run, runSettings)}
          onSettings={(next) => {
            // A new speed during a run goes to the run (#219); every change is the next run's too.
            const tell = speedChange(running, dockSettings(state.run, runSettings), next, state.runDefaults);
            if (tell) send(tell);
            setRunSettings(next);
          }}
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
            installAll(selections);
          }}
          onCancel={() => setMissingDialog(null)}
        />
      )}
      {notice && <Notice key={notice.title + notice.text.join()} spec={notice} onClose={() => setNotice(null)} />}
      {searchOpen && <CommandSearch commands={commands} onClose={() => setSearchOpen(false)} />}
      {backendOpen && (
        <BackendWindow
          url={url}
          connection={state.connection}
          engine={state.engine}
          info={{
            protocol: "paperboat/0",
            blocksCount: state.blocks.length,
            objectsCount: state.objects.length,
            stationView: state.stationView.view ?? state.view.mode,
            solver: state.runDefaults.solver ? solverShort(state.runDefaults.solver) : undefined,
            dt: state.runDefaults.dt,
            duration: state.runDefaults.duration,
            toolkits: state.toolkits.map((t) => ({ name: t.name, installed: t.installed, version: t.version })),
            variablesCount: state.variables.length,
          }}
          onConnect={setUrl}
          onClose={() => setBackendOpen(false)}
        />
      )}
      {consoleShown && <ConsoleWindow logs={state.logs} errors={errors} onBlock={showBlock} onClose={() => setConsoleShown(false)} />}
      {browser === "flow" && (
        <BrowserWindow
          title="Add a block"
          items={blockItems}
          counts={counts(nodes.filter(isBlock).map((n) => ({ type: n.data.def.type })))}
          place="canvas"
          flow={sheet.flow}
          onAdd={addBlock}
          onClose={() => setBrowser(null)}
        />
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
      {solverOpen && (
        <SolverDialog defaults={state.runDefaults} value={runSettings} onChange={setRunSettings} onClose={() => setSolverOpen(false)} />
      )}
      {lockModalOpen && (
        <LockDialog
          lock={projectLock}
          unlocked={unlocked}
          onUnlock={() => {
            setUnlocked(true);
            setLockModalOpen(false);
          }}
          onSave={(cfg) => {
            setProjectLock(cfg);
            setUnlocked(false);
          }}
          onClose={() => setLockModalOpen(false)}
        />
      )}
    </div>
  );

}
