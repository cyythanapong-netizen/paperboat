import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import type { RunSettings } from "./protocol";
import { RunOptions } from "./RunOptions";

/**
 * The run dock (#78): Run, Cancel, the next run's length and speed, and where the run is. It floats at
 * the bottom of the pages (glass: it sits over live content), so the top bar can be menus only.
 */
export function Dock({
  canRun,
  whyNot,
  running,
  status,
  statusText,
  defaults,
  settings,
  onSettings,
  onRun,
  onCancel,
}: {
  canRun: boolean;
  /** Why Run is off: said in its tooltip. */
  whyNot?: string;
  running: boolean;
  status?: string;
  statusText?: ReactNode;
  defaults: RunSettings;
  settings: RunSettings;
  onSettings: (v: RunSettings) => void;
  onRun: () => void;
  onCancel: () => void;
}) {
  return (
    <div className="dock" role="group" aria-label="Run">
      <button className="key run" disabled={!canRun} title={canRun ? "Run the flow" : whyNot} onClick={onRun}>
        ▶ Run
      </button>
      <button className="key" disabled={!running} title={running ? "Stop the run" : "No run in progress"} onClick={onCancel}>
        ■ Cancel
      </button>
      <RunOptions defaults={defaults} value={settings} onChange={onSettings} />
      {status && <span className={`run-status status-${status}`}>{statusText}</span>}
    </div>
  );
}

/**
 * File > Backend…: which backend the app talks to. A packaged app has one and never needs this; it is for
 * tinkerers pointing the app at another (an address, such as ws://localhost:8765). Esc or Close leaves.
 */
export function BackendWindow({
  url,
  connection,
  engine,
  onConnect,
  onClose,
}: {
  url: string;
  connection: string;
  engine: string;
  onConnect: (url: string) => void;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState(url);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);
  return (
    <section className="console-window backend-window" role="dialog" aria-label="Backend" data-own-keys>
      <header className="console-head">
        <h2>Backend</h2>
        <span className={`lbl conn-${connection}`}>{connection === "open" ? `connected: ${engine}` : connection}</span>
        <button className="key small" title="Close (Esc)" onClick={onClose}>
          Close
        </button>
      </header>
      <form
        className="backend-form"
        onSubmit={(e) => {
          e.preventDefault();
          onConnect(draft.trim());
        }}
      >
        <label className="lbl" htmlFor="backend-address">
          Address
        </label>
        <span className="well field">
          <input id="backend-address" autoFocus value={draft} spellCheck={false} onChange={(e) => setDraft(e.target.value)} size={28} />
        </span>
        <button className="key">Connect</button>
      </form>
      <p className="muted backend-note">The backend runs the domain: its blocks, its station and its physics. The app reconnects by itself when it restarts.</p>
    </section>
  );
}
