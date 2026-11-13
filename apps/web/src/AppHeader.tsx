// The app's top bar: the menus, the project's name, toolkits, the Console, the view mode, the UI
// lock, which backend, and the theme.

import type { EngineState } from "./engine";
import type { Command } from "./commands";
import type { Ref } from "react";
import { ProjectLabel } from "./ProjectUI";
import { StatusPill, Switch, ThemeToggle, Wordmark, type Mode, type Theme } from "./Shell";
import { MenuBar } from "./Menus";
import { ToolkitsButton } from "./Toolkits";
import type { LockConfig } from "./lock";
import { count } from "./words";

const MODE_OPTIONS: { value: Mode; label: string; hint: string }[] = [
  { value: "navigator", label: "navigator", hint: "One page at a time (Alt+M)" },
  { value: "commander", label: "commander", hint: "Station and Flow, the Board under them (Alt+M)" },
  { value: "lookout", label: "lookout", hint: "The Board, Station and Flow at its sides (Alt+M)" },
];

const CONN_TONE = { open: "ok", connecting: "busy", closed: "bad" } as const;

type Props = {
  state: EngineState;
  running: boolean;
  commands: Command[];
  /** The solver in use when the Run menu offers solvers. */
  solver?: string;
  projectTitle: string;
  dirty: boolean;
  /** Double-click the name in the bar: the project's title and the next .ppj name. */
  onRenameProject?: (name: string) => void;
  installToolkit: (name: string, version?: string) => void;
  uninstallToolkit: (name: string) => void;
  /** Toolkit operations asked for and not yet acknowledged, so the row says "installing…" at once. */
  requestedToolkits: Record<string, string>;
  restartBackend: () => void;
  toolkitVersions: (name: string) => void;
  errors: number;
  consoleShown: boolean;
  onConsole: () => void;
  /** The Console key: a press on it toggles, it does not dismiss and reopen the window. */
  consoleRef?: Ref<HTMLButtonElement>;
  aboutShown: boolean;
  onAbout: () => void;
  mode: Mode;
  setMode: (m: Mode) => void;
  allowedModes: Mode[];
  projectLock: LockConfig | null;
  locked: boolean;
  onLock: () => void;
  onBackend: () => void;
  themeChoice: Theme;
  setThemeChoice: (t: Theme) => void;
};

export function AppHeader({ state, running, commands, solver, projectTitle, dirty, onRenameProject, installToolkit, uninstallToolkit, requestedToolkits, restartBackend, toolkitVersions, errors, consoleShown, onConsole, consoleRef, aboutShown, onAbout, mode, setMode, allowedModes, projectLock, locked, onLock, onBackend, themeChoice, setThemeChoice }: Props) {
  return (
    <header className="bar">
      <Wordmark running={running} />
      <div className="menubar-group" data-own-keys>
        <MenuBar commands={commands} menuLabels={solver ? { run: `Run (${solver})` } : undefined} />
        <button
          className="menu-title"
          aria-expanded={aboutShown}
          title="About Paperboat"
          onClick={onAbout}
        >
          About
        </button>
      </div>
      <ProjectLabel name={projectTitle} dirty={dirty} onRename={onRenameProject} />
      <span className="spacer" />
      <ToolkitsButton
        toolkits={state.toolkits}
        connected={state.connection === "open"}
        requested={requestedToolkits}
        onInstall={installToolkit}
        onUninstall={uninstallToolkit}
        onRestart={restartBackend}
        onVersions={toolkitVersions}
      />
      <button
        ref={consoleRef}
        className={`key small${errors > 0 ? " err" : ""}${consoleShown ? " on" : ""}`}
        aria-pressed={consoleShown}
        title="Open the Console"
        onClick={onConsole}
      >
        {errors > 0 ? count(errors, "error") : "Console"}
      </button>
      {allowedModes.length > 1 && (
        <Switch
          label="View"
          value={mode}
          onChange={setMode}
          options={MODE_OPTIONS.filter((o) => allowedModes.includes(o.value))}
        />
      )}
      {projectLock && (
        <button
          className={`chip${locked ? " on" : ""}`}
          title={locked ? "UI is locked by project (click to unlock)" : "UI lock configured (unlocked)"}
          onClick={onLock}
        >
          {locked ? "🔒 Locked" : "🔓 Lock"}
        </button>
      )}
      <StatusPill
        label={state.connection === "open" ? state.engine : state.connection}
        labels={[state.engine, "connecting", "closed"]}
        tone={CONN_TONE[state.connection]}
        title="Which backend: File > Backend…"
        onClick={onBackend}
      />
      <ThemeToggle theme={themeChoice} onTheme={setThemeChoice} />
    </header>
  );
}
