import { Panel } from "@xyflow/react";
import type { GroupNode } from "./groups";

/** Where you are on the Flow canvas when a subsystem or story is open (#68): click a level to go back up. */
export function LevelCrumbs({ level, path, go, rename }: { level: string; path: GroupNode[]; go: (to: string | null) => void; rename: (id: string, label: string) => void }) {
  return (
    <Panel position="top-left" className="crumbs" aria-label="Where you are: click a level to go back up (Alt+↑ or Escape goes up one)">
      <button className="crumb" onClick={() => go(null)}>
        Flow
      </button>
      {path.map((g) => (
        <span key={g.id} className="crumb-step">
          <span className="crumb-sep">›</span>
          {g.id === level ? (
            <input
              className="crumb-here nodrag"
              value={g.data.label}
              size={Math.max(4, g.data.label.length)}
              title={`Rename this ${g.data.kind}`}
              onChange={(e) => rename(g.id, e.target.value)}
            />
          ) : (
            <button className="crumb" onClick={() => go(g.id)}>
              {g.data.label}
            </button>
          )}
        </span>
      ))}
      <span className="crumb-kind">{path[path.length - 1]?.data.kind}</span>
    </Panel>
  );
}
