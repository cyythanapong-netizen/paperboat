import type { ReactNode } from "react";

/**
 * The app's frame (apps/web/DESIGN.md): two view modes over a Bauhaus composition.
 * - Navigator: one page at a time, picked on the rail (Alt+1–4). The default.
 * - Commander: every page at once, as tiles.
 * Each page has a colour and a shape: Station a red square, Flow a yellow triangle,
 * Dashboard a blue circle, Console a black bar.
 */
export type Page = "station" | "flow" | "dashboard" | "console";
export type Mode = "navigator" | "commander";

export const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: "station", label: "Station", hint: "The setup: what's being worked on" },
  { id: "flow", label: "Flow", hint: "The blocks and wires" },
  { id: "dashboard", label: "Dashboard", hint: "Instruments during a run" },
  { id: "console", label: "Console", hint: "What the backend logged" },
];

/** A page's shape, in its colour. */
export function Glyph({ page, size = 18 }: { page: Page; size?: number }) {
  return (
    <svg className={`glyph glyph-${page}`} width={size} height={size} viewBox="0 0 20 20" aria-hidden="true">
      {page === "station" && <rect x="2" y="2" width="16" height="16" />}
      {page === "flow" && <polygon points="10,1.5 19,18.5 1,18.5" />}
      {page === "dashboard" && <circle cx="10" cy="10" r="8.5" />}
      {page === "console" && <rect x="1" y="7.5" width="18" height="5" />}
    </svg>
  );
}

/** The paper boat, from the same primitives: a triangle sail on a hull. */
export function Wordmark() {
  return (
    <span className="wordmark">
      <svg width="26" height="22" viewBox="0 0 26 22" aria-hidden="true">
        <polygon className="sail" points="13,1 13,14 5,14" />
        <polygon className="sail-back" points="14,4 14,14 20,14" />
        <polygon className="hull" points="1,15 25,15 20,21 6,21" />
      </svg>
      Paperboat
    </span>
  );
}

/**
 * The ground behind the glass: one square, triangle, circle and bar, large and still. The
 * shape of the page in view comes forward (Navigator); in Commander they all rest.
 */
export function Composition({ focus }: { focus: Page | null }) {
  return (
    <div className={`composition${focus ? ` focus-${focus}` : ""}`} aria-hidden="true">
      <div className="shape shape-station" />
      <div className="shape shape-flow" />
      <div className="shape shape-dashboard" />
      <div className="shape shape-console" />
    </div>
  );
}

/** Navigator's rail: the four pages, a count of errors on the Console. */
export function Rail({ page, onPage, errors }: { page: Page; onPage: (p: Page) => void; errors: number }) {
  return (
    <nav className="rail" aria-label="Pages">
      {PAGES.map((p, i) => (
        <button
          key={p.id}
          className={`rail-item${p.id === page ? " current" : ""}`}
          aria-current={p.id === page ? "page" : undefined}
          title={`${p.hint} (Alt+${i + 1})`}
          onClick={() => onPage(p.id)}
        >
          <Glyph page={p.id} size={20} />
          <span>{p.label}</span>
          {p.id === "console" && errors > 0 && <b className="badge">{errors}</b>}
        </button>
      ))}
    </nav>
  );
}

export function ModeSwitch({ mode, onMode }: { mode: Mode; onMode: (m: Mode) => void }) {
  return (
    <div className="mode-switch" role="radiogroup" aria-label="View mode">
      {(["navigator", "commander"] as const).map((m) => (
        <button
          key={m}
          role="radio"
          aria-checked={mode === m}
          className={mode === m ? "on" : undefined}
          title={m === "navigator" ? "One page at a time" : "Every page at once"}
          onClick={() => onMode(m)}
        >
          {m === "navigator" ? "Navigator" : "Commander"}
        </button>
      ))}
    </div>
  );
}

/** One page as a glass panel. In Commander its head names it and can bring it to Navigator. */
export function Panel({
  page,
  mode,
  current,
  onOpen,
  tools,
  children,
}: {
  page: Page;
  mode: Mode;
  current: boolean;
  onOpen: () => void;
  tools?: ReactNode;
  children: ReactNode;
}) {
  const label = PAGES.find((p) => p.id === page)!.label;
  return (
    <section className={`panel panel-${page}`} hidden={mode === "navigator" && !current} aria-label={label}>
      <header className="panel-head">
        <Glyph page={page} size={14} />
        <h2>{label}</h2>
        <div className="panel-tools">{tools}</div>
        {mode === "commander" && (
          <button className="panel-open" title={`Open ${label} on its own (Navigator)`} onClick={onOpen}>
            Open
          </button>
        )}
      </header>
      <div className="panel-body">{children}</div>
    </section>
  );
}
