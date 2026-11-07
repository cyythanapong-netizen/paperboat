// Squero Studio: OpenModelica. Provides StudioContext and mounts Lane B's slots exactly as their
// stubs declare: the popover on a component click, the sheet on Alt+V, the dock always.

import { ReactFlowProvider } from "@xyflow/react";
import { useShortcuts, type Command } from "@paperboat/shared";
import { useEffect, useMemo, useState } from "react";
import { Canvas } from "./canvas/Canvas.tsx";
import { Library, useClassIndex } from "./library/Library.tsx";
import { StudioContext } from "./model/types.ts";
import { OmcClient, type OmcStatus } from "./omc/omc.ts";
import { StudioDock } from "./dock/index.tsx";
import { ComponentPopover } from "./popover/index.tsx";
import { InterfaceSheet } from "./sheet/index.tsx";
import { TopBar } from "./shell/TopBar.tsx";
import { Simulate } from "./simulate/Simulate.tsx";
import { Split, type View } from "./split/Split.tsx";
import { useStudioStore } from "./store/index.tsx";

type Theme = "auto" | "light" | "dark";

function serverUrl(): string {
  const given = new URLSearchParams(location.search).get("server");
  if (given) return given;
  return location.port === "3048" || !location.host ? "ws://localhost:8790" : `ws://${location.host}`;
}

function useTheme(): [Theme, (t: Theme) => void] {
  const [theme, set] = useState<Theme>(() => (localStorage.getItem("studio-theme") as Theme | null) ?? "auto");
  useEffect(() => {
    if (theme === "auto") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = theme;
    localStorage.setItem("studio-theme", theme);
  }, [theme]);
  return [theme, set];
}

export function App() {
  const [status, setStatus] = useState<OmcStatus>({ state: "checking", version: "" });
  const [omc, setOmc] = useState<OmcClient | null>(null);
  useEffect(() => {
    const client = new OmcClient(serverUrl(), setStatus);
    setOmc(client);
    return () => client.close();
  }, []);
  return omc ? (
    <ReactFlowProvider>
      <Studio omc={omc} status={status} />
    </ReactFlowProvider>
  ) : null;
}

function Studio({ omc, status }: { omc: OmcClient; status: OmcStatus }) {
  const { api, actions, error } = useStudioStore(omc, status);
  const [popover, setPopover] = useState<string | null>(null);
  const [sheet, setSheet] = useState(false);
  const [side, setSide] = useState(true);
  const [view, setView] = useState<View>("mo");
  const [theme, setTheme] = useTheme();
  const classes = useClassIndex(omc);

  const commands = useMemo<Command[]>(
    () => [
      { id: "undo", label: "Undo", menu: "edit", group: "history", shortcut: "Ctrl+Z", enabled: actions.canUndo, run: actions.undo },
      { id: "redo", label: "Redo", menu: "edit", group: "history", shortcut: "Ctrl+Shift+Z", enabled: actions.canRedo, run: actions.redo },
      { id: "side", label: "Source and Simulate", menu: "view", group: "panes", checked: side, run: () => setSide((s) => !s) },
      { id: "mo", label: ".mo", menu: "view", group: "source", checked: view === "mo", run: () => (setView("mo"), setSide(true)) },
      { id: "cpp", label: "causal ODE c++", menu: "view", group: "source", checked: view === "cpp", run: () => (setView("cpp"), setSide(true)) },
      { id: "sheet", label: "Interface sheet", menu: "view", group: "sheet", shortcut: "Alt+V", chord: { key: "v", alt: true }, run: () => setSheet((s) => !s) },
      ...(["auto", "light", "dark"] as Theme[]).map((t): Command => ({ id: `theme-${t}`, label: t, menu: "view", group: "theme", checked: theme === t, run: () => setTheme(t) })),
    ],
    [actions.canUndo, actions.canRedo, actions.undo, actions.redo, side, view, theme, setTheme],
  );
  useShortcuts(commands);

  const model = api?.model;
  useEffect(() => {
    if (popover && model && !model.components.some((c) => c.name === popover)) setPopover(null);
  }, [popover, model]);

  return (
    <StudioContext.Provider value={api}>
      <div className={`studio${side ? " with-side" : ""}`}>
        <TopBar status={status} model={model?.name} commands={commands} />
        {api && model ? (
          <>
            <Library omc={omc} model={model.name} onPlace={(cls) => void actions.place(cls, { x: 200, y: 160, rotation: 0 })} />
            <Canvas api={api} actions={actions} classes={classes} onOpen={setPopover} />
            {side && (
              <aside className="side">
                <Split model={model} actions={actions} view={view} onView={setView} />
                <Simulate actions={actions} theme={theme} />
              </aside>
            )}
            {popover && <ComponentPopover component={popover} onClose={() => setPopover(null)} />}
            <InterfaceSheet open={sheet} onClose={() => setSheet(false)} />
            <div className="dock-slot">
              <StudioDock />
            </div>
          </>
        ) : (
          <p className="opening" role="status">
            {error || (status.state === "failed" ? "No Studio server: start one with squero-om" : "opening the model…")}
          </p>
        )}
      </div>
    </StudioContext.Provider>
  );
}
