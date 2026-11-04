import type { KeyboardEvent, ReactNode } from "react";
import { Logo } from "./Logo";

/**
 * The app's frame (apps/web/DESIGN.md): two view modes.
 * - Navigator: one page at a time, picked on the rail (Alt+1–4). The default.
 * - Commander: every page at once, as tiles.
 * Pages are numbered for their shortcuts: 1 Station, 2 Flow, 3 Dashboard. The Console is not a page: it floats over them.
 */
export type Page = "station" | "flow" | "dashboard";
export type Mode = "navigator" | "commander";
export type Theme = "auto" | "light" | "dark";

export const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: "station", label: "Station", hint: "The setup: what's being worked on" },
  { id: "flow", label: "Flow", hint: "The blocks and wires" },
  { id: "dashboard", label: "Dashboard", hint: "Instruments during a run" },
];

const number = (p: Page) => PAGES.findIndex((x) => x.id === p) + 1;

/** The mark and the name. */
export function Wordmark() {
  return (
    <span className="wordmark">
      <Logo height={24} />
      paperboat
    </span>
  );
}

/**
 * A choice of one, instrument-panel style: the chosen option sits in brackets in bold ink,
 * the others plain and grey. Click one; arrow keys move the choice.
 */
export function Switch<T extends string>({
  label,
  options,
  value,
  onChange,
}: {
  label: string;
  options: { value: T; label: string; hint?: string }[];
  value: T;
  onChange: (v: T) => void;
}) {
  const i = Math.max(0, options.findIndex((o) => o.value === value));
  const onKey = (e: KeyboardEvent) => {
    const d = e.key === "ArrowRight" || e.key === "ArrowDown" ? 1 : e.key === "ArrowLeft" || e.key === "ArrowUp" ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    onChange(options[(i + d + options.length) % options.length].value);
  };
  return (
    <div className="switch" role="radiogroup" aria-label={label} onKeyDown={onKey}>
      <span className="lbl">{label}</span>
      {options.map((o) => (
        <button
          key={o.value}
          role="radio"
          aria-checked={o.value === value}
          tabIndex={o.value === value ? 0 : -1}
          className={`opt${o.value === value ? " on" : ""}`}
          title={o.hint}
          onClick={() => onChange(o.value)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

/** Navigator's rail: the pages; the current one is boxed in ink on panel grey, the rest sit greyed on the rail. */
export function Rail({ page, onPage }: { page: Page; onPage: (p: Page) => void }) {
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
          <span className="num">{i + 1}</span>
          <span className="name">{p.label}</span>
        </button>
      ))}
    </nav>
  );
}

/** One page as a section. In Commander its head can bring it to Navigator. */
export function Panel({
  page,
  mode,
  current,
  onOpen,
  info,
  actions,
  children,
}: {
  page: Page;
  mode: Mode;
  current: boolean;
  onOpen: () => void;
  info?: ReactNode;
  /** Keys in the head, before Open (the page's Add). */
  actions?: ReactNode;
  children: ReactNode;
}) {
  const label = PAGES.find((p) => p.id === page)!.label;
  return (
    <section className={`panel panel-${page}`} hidden={mode === "navigator" && !current} aria-label={label}>
      <header className="panel-head">
        <span className="num">{number(page)}</span>
        <h2>{label}</h2>
        <span className="panel-info lbl">{info}</span>
        {actions}
        {mode === "commander" && (
          <button className="key small" title={`Show ${label} on its own (Navigator)`} onClick={onOpen}>
            Open
          </button>
        )}
      </header>
      <div className="panel-body">{children}</div>
    </section>
  );
}
