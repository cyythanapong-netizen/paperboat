// The commands (#78): what the menus, the command search and the shortcuts all run. Only what the
// app already does; backends' commands come with their issue (#85).

import type { MutableRefObject } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, Project, RunSettings, Variables } from "./protocol";
import { ago, changed, type Stored } from "./project";
import type { Mode, Page, PageInfo, Theme } from "./Shell";
import { isModeAllowed, isPageAllowed, type LockConfig } from "./lock";
import { SEARCH_CHORD, type Command } from "./commands";
import { availableSolvers, SOLVER_LABELS, solverShort, type Solver } from "./solvers";
import { isBlock, type FlowNode } from "./flowModel";
import type { FlowControls } from "./FlowEditor";
import type { StationControls } from "./StationGraph";
import { count } from "./words";

export type SolverOption = Solver;

/** Whether the Run menu offers solvers (the backend or a block has one), and the one in use. */
export function solverChoice(state: EngineState, nodes: FlowNode[], runSettings: RunSettings) {
  const usesSolvers = Boolean(
    state.runDefaults.solver ||
    state.blocks.some((b) => b.type === "integrator" || b.type === "lag" || b.params?.some((p) => p.id === "solver")) ||
    state.objects.some((o) => o.props?.some((p) => p.id === "solver")) ||
    nodes.some((n) => isBlock(n) && (n.data.def.type === "integrator" || n.data.def.type === "lag" || n.data.params?.solver !== undefined)),
  );
  const solvers = availableSolvers(state.runDefaults);
  const asked = ((runSettings.solver ?? state.runDefaults.solver ?? "rk4").toLowerCase()) as SolverOption;
  // A project saved against another engine may name a solver this one hasn't; show the first it has.
  const currentSolver = solvers.includes(asked) ? asked : (solvers[0] ?? "rk4");
  return { usesSolvers, currentSolver, solvers };
}

/** Everything the commands read and do, as the app has it now. */
export type CommandContext = {
  state: EngineState;
  send: (m: ClientMsg) => void;
  // File
  openFile: () => Promise<void>;
  saveProject: (as: boolean) => Promise<void>;
  lastSession: Stored | undefined;
  restore: (s: Stored) => void;
  recent: Stored[];
  openProject: (p: Project, name: string) => void;
  projectLock: LockConfig | null;
  unlocked: boolean;
  locked: boolean;
  lockedArrange: boolean;
  setLockModalOpen: (open: boolean) => void;
  setBackendOpen: (open: boolean) => void;
  restartBackend: () => void;
  // Edit
  history: { undo: () => void; redo: () => void; canUndo: boolean; canRedo: boolean };
  undoOpen: (() => void) | null;
  flow: MutableRefObject<FlowControls | null>;
  stationCanvas: MutableRefObject<StationControls | null>;
  setBrowser: (b: "flow" | "station" | null) => void;
  running: boolean;
  variables: Variables;
  varsShown: "open" | "closed";
  setVarsShown: (v: "open" | "closed") => void;
  // View
  setSearchOpen: (open: boolean) => void;
  mode: Mode;
  setMode: (m: Mode) => void;
  allowedModes: Mode[];
  pages: PageInfo[];
  page: Page;
  setPage: (p: Page) => void;
  open: (p: Page) => void;
  stationShown: boolean;
  setStationShown: (shown: boolean) => void;
  stationDrawer: { open: boolean };
  flowDrawer: { open: boolean };
  view: EngineState["stationView"]["view"];
  themeChoice: Theme;
  setThemeChoice: (t: Theme) => void;
  consoleShown: boolean;
  setConsoleShown: (shown: boolean) => void;
  aboutShown: boolean;
  setAboutShown: (shown: boolean) => void;
  errors: number;
  gridOn: boolean;
  setGridChoice: (g: "auto" | "on" | "off") => void;
  cameraProjection: "persp" | "iso";
  setCameraProjection: (p: "persp" | "iso") => void;
  arrangeCanvas: (which: "flow" | "station") => boolean;
  arrange: boolean;
  setArrange: (on: boolean) => void;
  // Replay (#284)
  /** Open a saved run record; a rejected file is reported in the Console. */
  loadRecord: () => Promise<void>;
  /** Save this run's samples and Station history. */
  saveRecord: () => Promise<void>;
  startReplay: () => void;
  // Run
  canRun: boolean;
  whyNotRun: string;
  run: () => void;
  usesSolvers: boolean;
  currentSolver: SolverOption;
  solvers: SolverOption[];
  setRunSettings: (f: (prev: RunSettings) => RunSettings) => void;
  setSolverOpen: (open: boolean) => void;
};

export function menuCommands(c: CommandContext): Command[] {
  const {
    state, send, openFile, saveProject, lastSession, restore, recent, openProject, projectLock, unlocked, locked, lockedArrange, setLockModalOpen, setBackendOpen, restartBackend,
    history, undoOpen, flow, stationCanvas, setBrowser, running, variables, varsShown, setVarsShown,
    setSearchOpen, mode, setMode, allowedModes, pages, page, setPage, open, stationShown, setStationShown, stationDrawer, flowDrawer, view, themeChoice, setThemeChoice, consoleShown, setConsoleShown, aboutShown, setAboutShown, errors,
    gridOn, setGridChoice, cameraProjection, setCameraProjection, arrangeCanvas, arrange, setArrange,
    canRun, whyNotRun, run, usesSolvers, currentSolver, solvers, setRunSettings, setSolverOpen,
    loadRecord, saveRecord, startReplay,
  } = c;
  const flowInView = mode === "commander" || (mode === "lookout" ? flowDrawer.open : page === "flow");
  const stationInView = stationShown && view === "graph" && (mode === "commander" || (mode === "lookout" ? stationDrawer.open : page === "station"));
  const arrangeVisible = () => {
    if (flowInView) arrangeCanvas("flow");
    if (stationInView) arrangeCanvas("station");
  };
  /** An Edit action on the canvas: on while the Flow is in view and the canvas allows it. */
  const onCanvas = (can: (c: FlowControls["can"]) => boolean) => () => flowInView && !!flow.current && can(flow.current.can);
  const canvasWhy = (why: string) => (flowInView ? why : "Open the Flow page first");

  return [
    { id: "file.open", label: "Open…", menu: "file", group: "project", shortcut: "Ctrl+O", chord: { key: "o", ctrl: true }, enabled: state.connection === "open", why: "Not connected to a backend", run: () => void openFile() },
    { id: "file.save", label: "Save", menu: "file", group: "project", shortcut: "Ctrl+S", chord: { key: "s", ctrl: true }, enabled: !!state.engine, why: "Not connected to a backend yet", run: () => void saveProject(false) },
    { id: "file.saveAs", label: "Save as…", menu: "file", group: "project", shortcut: "Ctrl+Shift+S", chord: { key: "s", ctrl: true, shift: true }, enabled: !!state.engine, why: "Not connected to a backend yet", run: () => void saveProject(true) },
    { id: "file.openRecord", label: "Open run record…", menu: "file", group: "record", run: () => void loadRecord() },
    { id: "file.saveRecord", label: "Save run record…", menu: "file", group: "record", enabled: !!state.traces.span(), why: "No run to save yet", run: () => void saveRecord() },
    { id: "file.restore", label: "Restore last session", menu: "file", group: "session", enabled: !!lastSession, why: "No unsaved session to restore", run: () => lastSession && restore(lastSession) },
    { id: "file.lock", label: locked ? "Unlock UI…" : projectLock ? "UI Lock Settings…" : "Lock UI…", menu: "file", group: "session", run: () => setLockModalOpen(true) },
    ...recent.map((s): Command => ({ id: `file.recent.${s.name}`, label: s.name, menu: "file", group: "recent", meta: ago(s.savedAt, Date.now()), enabled: state.connection === "open", why: "Not connected to a backend", run: () => openProject(s.project, s.name) })),
    { id: "file.backend", label: "Backend…", menu: "file", group: "backend", run: () => setBackendOpen(true) },
    { id: "file.restartBackend", label: "Restart backend", menu: "file", group: "backend", enabled: state.connection === "open", why: "Not connected to a backend", run: restartBackend },

    { id: "edit.undo", label: "Undo", menu: "edit", group: "history", shortcut: "Ctrl+Z", enabled: history.canUndo, why: "Nothing to undo on the Flow canvas", run: history.undo },
    { id: "edit.undoOpen", label: "Undo open project", menu: "edit", group: "history", enabled: !!undoOpen, why: "No project was just opened", run: () => undoOpen?.() },
    { id: "edit.redo", label: "Redo", menu: "edit", group: "history", shortcut: "Ctrl+Shift+Z", enabled: history.canRedo, why: "Nothing to redo", run: history.redo },
    { id: "edit.cut", label: "Cut", menu: "edit", group: "clipboard", shortcut: "Ctrl+X", enabled: onCanvas((c) => c.copy), why: canvasWhy("Select blocks first"), run: () => flow.current?.cut() },
    { id: "edit.copy", label: "Copy", menu: "edit", group: "clipboard", shortcut: "Ctrl+C", enabled: onCanvas((c) => c.copy), why: canvasWhy("Select blocks first"), run: () => flow.current?.copy() },
    { id: "edit.paste", label: "Paste", menu: "edit", group: "clipboard", shortcut: "Ctrl+V", enabled: onCanvas((c) => c.paste), why: canvasWhy("Copy blocks first"), run: () => flow.current?.paste() },
    { id: "edit.delete", label: "Delete", menu: "edit", group: "clipboard", shortcut: "Del", enabled: onCanvas((c) => c.remove), why: canvasWhy("Select blocks or wires first"), run: () => flow.current?.remove() },
    { id: "edit.comment", label: "Comment out", menu: "edit", group: "clipboard", shortcut: "Ctrl+/", chord: { key: "/", ctrl: true }, enabled: onCanvas((c) => c.comment), why: canvasWhy("Select blocks first"), run: () => flow.current?.comment("out") },
    { id: "edit.bypass", label: "Comment through", menu: "edit", group: "clipboard", shortcut: "Ctrl+Shift+Y", chord: { key: "y", ctrl: true, shift: true }, enabled: onCanvas((c) => c.comment), why: canvasWhy("Select blocks first"), run: () => flow.current?.comment("through") },
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
      run: () => {
        setStationShown(true);
        setBrowser("station");
      },
    },
    { id: "edit.variables", label: "Variables…", menu: "edit", group: "panels", shortcut: "Alt+V", chord: { key: "v", alt: true }, meta: String(variables.length), run: () => setVarsShown(varsShown === "open" ? "closed" : "open") },

    { id: "view.search", label: "Command search…", menu: "view", group: "search", shortcut: "Ctrl+K", chord: SEARCH_CHORD, quiet: true, run: () => setSearchOpen(true) },
    ...pages.map((p, i): Command => {
      const allowed = isPageAllowed(p.id, projectLock, unlocked);
      return {
        id: `view.page.${p.id}`,
        label: p.label,
        menu: "view",
        group: "pages",
        shortcut: `Alt+${i + 1}`,
        chord: { key: String(i + 1), alt: true },
        checked: mode === "navigator" && page === p.id,
        enabled: allowed,
        why: allowed ? undefined : "Locked by the project",
        run: () => { if (allowed) open(p.id); },
      };
    }),
    { id: "view.station", label: "Station page", menu: "view", group: "layout", checked: stationShown, run: () => setStationShown(!stationShown) },
    { id: "view.mode.navigator", label: "Navigator", menu: "view", group: "mode", checked: mode === "navigator", enabled: isModeAllowed("navigator", projectLock, unlocked), why: isModeAllowed("navigator", projectLock, unlocked) ? undefined : "Locked by the project", run: () => setMode("navigator") },
    { id: "view.mode.commander", label: "Commander", menu: "view", group: "mode", checked: mode === "commander", enabled: isModeAllowed("commander", projectLock, unlocked), why: isModeAllowed("commander", projectLock, unlocked) ? undefined : "Locked by the project", run: () => setMode("commander") },
    { id: "view.mode.lookout", label: "Lookout", menu: "view", group: "mode", checked: mode === "lookout", enabled: isModeAllowed("lookout", projectLock, unlocked), why: isModeAllowed("lookout", projectLock, unlocked) ? undefined : "Locked by the project", run: () => setMode("lookout") },
    { id: "view.mode.switch", label: "Switch view mode", menu: "view", group: "mode", shortcut: "Alt+M", chord: { key: "m", alt: true }, enabled: allowedModes.length > 1, why: allowedModes.length <= 1 ? "Locked to a single mode by the project" : undefined, run: () => { if (allowedModes.length > 1) { const nextIdx = (allowedModes.indexOf(mode) + 1) % allowedModes.length; setMode(allowedModes[nextIdx] as Mode); } } },
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
      id: "view.camera.persp",
      label: "Perspective camera",
      menu: "view",
      group: "windows",
      checked: cameraProjection === "persp",
      enabled: view === "scene" && state.view.mode === "3d",
      why: "The Station is not a 3D scene",
      run: () => setCameraProjection("persp"),
    },
    {
      id: "view.camera.iso",
      label: "Isometric camera",
      menu: "view",
      group: "windows",
      checked: cameraProjection === "iso",
      enabled: view === "scene" && state.view.mode === "3d",
      why: "The Station is not a 3D scene",
      run: () => setCameraProjection("iso"),
    },
    {
      id: "view.arrangeBlocks",
      label: "Arrange blocks",
      menu: "view",
      group: "canvas",
      enabled: () => !running && ((flowInView && !!flow.current?.can.arrange) || (stationInView && !!stationCanvas.current?.can.arrange)),
      why: running ? "Stop the run to arrange blocks" : flowInView || stationInView ? "There are no blocks to arrange" : "Open the Flow or Station graph first",
      run: arrangeVisible,
    },
    {
      id: "view.arrange",
      label: "Arrange board",
      menu: "view",
      group: "windows",
      checked: arrange,
      enabled: !lockedArrange,
      why: lockedArrange ? "Locked by the project" : undefined,
      run: () => {
        if (lockedArrange) return;
        if (!arrange && mode === "navigator") setPage("dashboard");
        setArrange(!arrange);
      },
    },
    { id: "view.about", label: "About Paperboat", menu: "view", group: "about", run: () => setAboutShown(!aboutShown) },

    {
      id: "run.run",
      label: "Run",
      menu: "run",
      group: "run",
      meta: usesSolvers ? solverShort(currentSolver) : undefined,
      enabled: canRun,
      why: whyNotRun || undefined,
      run,
    },
    { id: "run.cancel", label: "Cancel", menu: "run", group: "run", enabled: running, why: "No run in progress", run: () => send({ type: "run.cancel" }) },
    {
      id: "run.replay",
      label: "Replay last run",
      menu: "run",
      group: "run",
      enabled: !running && !!state.traces.span(),
      why: running ? "A run is in progress" : "No run to replay yet",
      run: startReplay,
    },
    ...(usesSolvers
      ? solvers.map((s) => ({
          id: `run.solver.${s}`,
          label: SOLVER_LABELS[s],
          menu: "run" as const,
          group: "solver",
          checked: currentSolver === s,
          meta: currentSolver === s ? "active" : undefined,
          run: () => setRunSettings((prev) => changed({ ...prev, solver: s }, state.runDefaults)),
        }))
      : []),
    ...(usesSolvers
      ? [{ id: "run.solverSettings", label: "Solver settings…", menu: "run" as const, group: "solver-settings", run: () => setSolverOpen(true) }]
      : []),
  ];
}
