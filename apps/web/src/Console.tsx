import { useState } from "react";
import type { EngineState } from "./engine";

type Log = EngineState["logs"][number];

/** One line as plain text, for a bug report or a chat: level, block, message. */
export function logText(l: Log): string {
  const text = l.node && l.text.startsWith(`${l.node}: `) ? l.text.slice(l.node.length + 2) : l.text;
  return `[${l.level}]${l.node ? ` ${l.node}:` : ""} ${text}`;
}

/** A small key that copies `text` and says so for a moment. */
function CopyKey({ text, label = "copy", title, className = "log-copy" }: { text: () => string; label?: string; title: string; className?: string }) {
  const [done, setDone] = useState(false);
  return (
    <button
      className={className}
      title={title}
      onClick={() =>
        void navigator.clipboard?.writeText(text()).then(() => {
          setDone(true);
          setTimeout(() => setDone(false), 1200);
        })
      }
    >
      {done ? "copied" : label}
    </button>
  );
}

/**
 * The Console: what the backend logged, newest last. A line that names a block (the one
 * that wrote it, or failed) has a button that shows the block on the Flow page; every
 * line has a copy key (on hover) that copies it as plain text.
 */
export function Console({ logs, onBlock }: { logs: EngineState["logs"]; onBlock: (id: string) => void }) {
  if (logs.length === 0) return <p className="empty">Nothing yet. Runs, errors and what blocks log appear here.</p>;
  return (
    <ol className="logs screen">
      {logs.map((l, i) => (
        <li key={i} className={`log-${l.level}`}>
          {l.node && (
            <button className="log-node" title="Show this block on the Flow page" onClick={() => onBlock(l.node!)}>
              {l.node}
            </button>
          )}
          {l.node && l.text.startsWith(`${l.node}: `) ? l.text.slice(l.node.length + 2) : l.text}
          <CopyKey text={() => logText(l)} title="Copy this line" />
        </li>
      ))}
    </ol>
  );
}

/** The Console as a window floating in the middle of the screen; the header's Console key toggles it. */
export function ConsoleWindow({
  logs,
  errors,
  onBlock,
  onClose,
}: {
  logs: EngineState["logs"];
  errors: number;
  onBlock: (id: string) => void;
  onClose: () => void;
}) {
  return (
    <section className="console-window" role="dialog" aria-label="Console">
      <header className="console-head">
        <h2>Console</h2>
        <span className="lbl">{`${logs.length} ${logs.length === 1 ? "line" : "lines"}${errors ? ` · ${errors} ${errors === 1 ? "error" : "errors"}` : ""}`}</span>
        {logs.length > 0 && <CopyKey text={() => logs.map(logText).join("\n")} label="Copy all" title="Copy every line as plain text" className="key small" />}
        <button className="key small" title="Close the Console (Esc)" onClick={onClose}>
          Close
        </button>
      </header>
      <div className="console-body">
        <Console logs={logs} onBlock={onBlock} />
      </div>
    </section>
  );
}
