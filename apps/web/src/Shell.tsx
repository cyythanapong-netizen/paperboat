import type { KeyboardEvent, ReactNode } from "react";

/**
 * The app's frame (apps/web/DESIGN.md): two view modes.
 * - Navigator: one page at a time, picked on the rail (Alt+1–4). The default.
 * - Commander: every page at once, as tiles.
 * Pages are numbered for their shortcuts: 1 Station, 2 Flow, 3 Dashboard, 4 Console.
 */
export type Page = "station" | "flow" | "dashboard" | "console";
export type Mode = "navigator" | "commander";
export type Theme = "auto" | "light" | "dark";

export const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: "station", label: "station", hint: "The setup: what's being worked on" },
  { id: "flow", label: "flow", hint: "The blocks and wires" },
  { id: "dashboard", label: "dashboard", hint: "Instruments during a run" },
  { id: "console", label: "console", hint: "What the backend logged" },
];

const number = (p: Page) => PAGES.findIndex((x) => x.id === p) + 1;

/** The paper boat, as a line drawing. */
export function Wordmark() {
  return (
    <span className="wordmark">
      <svg width="22" height="18" viewBox="0 0 22 18" aria-hidden="true">
        <path d="M11 1 V12 H4 Z" />
        <path d="M12 4 V12 H17 Z" />
        <path d="M1 13 H21 L17 17 H5 Z" />
      </svg>
      paperboat
    </span>
  );
}

/**
 * A flat switch: an outlined track whose knob sits on the chosen option, and the chosen
 * option's name in bold ink (the rest grey). Two options sit either side of the track;
 * three or more sit below it. Click a name, or the track to step; arrow keys move it.
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
  const step = (d: number) => onChange(options[(i + d + options.length) % options.length].value);
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "ArrowRight" || e.key === "ArrowDown") (e.preventDefault(), step(1));
    if (e.key === "ArrowLeft" || e.key === "ArrowUp") (e.preventDefault(), step(-1));
  };
  const name = (o: (typeof options)[number]) => (
    <button
      key={o.value}
      role="radio"
      aria-checked={o.value === value}
      tabIndex={-1}
      className={`opt${o.value === value ? " on" : ""}`}
      title={o.hint}
      onClick={() => onChange(o.value)}
    >
      {o.label}
    </button>
  );
  const track = (
    <button
      className={`track n${options.length}`}
      aria-label={`${label}: ${options[i].label}`}
      onClick={() => step(1)}
      onKeyDown={onKey}
      style={{ ["--pos" as string]: options.length > 1 ? i / (options.length - 1) : 0 }}
    >
      <span className="knob" />
    </button>
  );
  return (
    <div className={`switch${options.length > 2 ? " stacked" : ""}`} role="radiogroup" aria-label={label}>
      <span className="lbl">{label}</span>
      {options.length === 2 ? (
        <>
          {name(options[0])}
          {track}
          {name(options[1])}
        </>
      ) : (
        <span className="switch-stack">
          {track}
          <span className="opts">{options.map(name)}</span>
        </span>
      )}
    </div>
  );
}

/** Navigator's rail: the four pages; the current one has a bar beside it and a bold name. */
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
          <span className="num">{i + 1}</span>
          <span className="name">{p.label}</span>
          {p.id === "console" && errors > 0 && <span className="count" title={`${errors} errors`}>{errors}</span>}
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
  children,
}: {
  page: Page;
  mode: Mode;
  current: boolean;
  onOpen: () => void;
  info?: ReactNode;
  children: ReactNode;
}) {
  const label = PAGES.find((p) => p.id === page)!.label;
  return (
    <section className={`panel panel-${page}`} hidden={mode === "navigator" && !current} aria-label={label}>
      <header className="panel-head">
        <span className="num">{number(page)}</span>
        <h2>{label}</h2>
        <span className="panel-info lbl">{info}</span>
        {mode === "commander" && (
          <button className="key small" title={`Show ${label} on its own (Navigator)`} onClick={onOpen}>
            open
          </button>
        )}
      </header>
      <div className="panel-body">{children}</div>
    </section>
  );
}
