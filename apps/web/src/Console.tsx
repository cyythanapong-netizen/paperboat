import type { EngineState } from "./engine";

/**
 * The Console: what the backend logged, newest last. A line that names a block (the one
 * that wrote it, or failed) has a button that shows the block on the Flow page.
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
