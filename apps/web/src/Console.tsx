import type { EngineState } from "./engine";

/**
 * The Console: what the backend logged, newest last. A line that names a block (the one
 * that wrote it, or failed) has a button that shows the block on the Flow page.
 */
export function Console({ logs, onBlock }: { logs: EngineState["logs"]; onBlock: (id: string) => void }) {
  if (logs.length === 0) return <p className="empty">Nothing yet. Runs, errors and what blocks log appear here.</p>;
  return (
    <ol className="logs">
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
