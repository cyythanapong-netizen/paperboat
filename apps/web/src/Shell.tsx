import { useLayoutEffect, useRef, type CSSProperties, type KeyboardEvent, type PointerEvent, type ReactNode } from "react";
import { Logo } from "./Logo";

/**
 * The app's frame (apps/web/DESIGN.md): two view modes.
 * - Navigator: one page at a time, picked on the rail (Alt+1–3). The pages sit in a column and
 *   the column slides to the one picked. The default.
 * - Commander: Station and Flow side by side, the Board a drawer under them.
 * The Board is the `dashboard` page in code. The Console is not a page: it floats over them.
 */
export type Page = "station" | "flow" | "dashboard";
export type Mode = "navigator" | "commander";
export type Theme = "auto" | "light" | "dark";

export const PAGES: { id: Page; label: string; hint: string }[] = [
  { id: "station", label: "Station", hint: "The setup: what's being worked on" },
  { id: "flow", label: "Flow", hint: "The blocks and wires" },
  { id: "dashboard", label: "Board", hint: "Instruments during a run" },
];

const index = (p: Page) => PAGES.findIndex((x) => x.id === p);

/** A page's glyph, drawn in strokes so it takes the text colour. The Station's also marks what is linked to it. */
export function PageIcon({ page, className = "page-icon" }: { page: Page; className?: string }) {
  return (
    <svg className={className} viewBox="0 0 18 18" aria-hidden="true">
      {page === "station" ? (
        <>
          <path d="M9 2l6 3.5v7L9 16l-6-3.5v-7z" />
          <path d="M3 5.5L9 9l6-3.5M9 9v7" />
        </>
      ) : page === "flow" ? (
        <>
          <rect x="1.5" y="3" width="5" height="4" rx="1" />
          <rect x="11.5" y="11" width="5" height="4" rx="1" />
          <path d="M6.5 5h3v8h2" />
        </>
      ) : (
        <>
          <rect x="2" y="2" width="6" height="8" rx="1" />
          <rect x="10" y="2" width="6" height="4" rx="1" />
          <rect x="10" y="8" width="6" height="8" rx="1" />
          <rect x="2" y="12" width="6" height="4" rx="1" />
        </>
      )}
    </svg>
  );
}

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

const reduceMotion = () => typeof matchMedia === "function" && matchMedia("(prefers-reduced-motion: reduce)").matches;
/** The brackets glide as one pair, on the page column's own curve and time, so the rail and the page move as one. */
const GLIDE = "top var(--slide), bottom var(--slide), left var(--slide), right var(--slide)";
/** Room between a key's icon and name and the brackets round them. */
const PAD = 8;

/**
 * Navigator's rail: the pages share its whole height, and the current one sits in ink
 * brackets, like a chosen option in a switch (`[ navigator ]`). On a change the pair glides
 * to the new key with the page column. They are
 * placed from the key's face (icon and name) plus room, so they never touch the name.
 * ↑/↓ and the wheel over the rail move between pages.
 */
export function Rail({ page, onPage, hidden }: { page: Page; onPage: (p: Page) => void; hidden?: boolean }) {
  const rail = useRef<HTMLElement>(null);
  const brackets = useRef<HTMLSpanElement[]>([]);
  const at = index(page);
  const was = useRef(-1);
  const onPageRef = useRef(onPage);
  onPageRef.current = onPage;
  const atRef = useRef(at);
  atRef.current = at;

  useLayoutEffect(() => {
    const r = rail.current;
    if (!r) return;
    const place = (dir: number) => {
      const face = r.querySelectorAll<HTMLElement>(".rail-face")[atRef.current];
      if (!face) return;
      const rb = r.getBoundingClientRect(), fb = face.getBoundingClientRect();
      const across = rb.height >= rb.width; // a column on wide screens, a row along the bottom on narrow ones
      // The brackets stand as far apart as the widest face needs, the same on every key.
      const faces = [...r.querySelectorAll<HTMLElement>(".rail-face")].map((f) => f.getBoundingClientRect());
      const side = across ? Math.max(2, (rb.width - Math.max(...faces.map((f) => f.width))) / 2 - PAD) : fb.left - rb.left - PAD;
      const band = Math.max(2, (rb.height - fb.height) / 2 - PAD); // in a row, the brackets fit its height
      const box = {
        top: across ? fb.top - rb.top - PAD : band,
        bottom: across ? rb.bottom - fb.bottom - PAD : band,
        left: side,
        right: across ? side : rb.right - fb.right - PAD,
      };
      const still = !dir || reduceMotion();
      brackets.current.forEach((el, k) => {
        if (!el) return;
        el.style.transition = still ? "none" : GLIDE;
        el.style.top = `${box.top}px`;
        el.style.bottom = `${box.bottom}px`;
        if (k === 0) el.style.left = `${box.left}px`;
        else el.style.right = `${box.right}px`;
      });
    };
    place(was.current >= 0 ? Math.sign(at - was.current) : 0);
    was.current = at;
    // A resize re-places them at once. Skip the call the observer makes as it starts, or it
    // would cut the flight just begun short.
    let started = false;
    const ro = new ResizeObserver(() => (started ? place(0) : (started = true)));
    ro.observe(r);
    return () => ro.disconnect();
  }, [at]);

  // The wheel over the rail steps a page at a time (not passive: it must not scroll anything else).
  useLayoutEffect(() => {
    const r = rail.current;
    if (!r) return;
    let last = 0;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const now = performance.now();
      if (now - last < 450 || Math.abs(e.deltaY) < 4) return;
      last = now;
      const next = Math.max(0, Math.min(PAGES.length - 1, atRef.current + Math.sign(e.deltaY)));
      if (next !== atRef.current) onPageRef.current(PAGES[next].id);
    };
    r.addEventListener("wheel", onWheel, { passive: false });
    return () => r.removeEventListener("wheel", onWheel);
  }, []);

  const onKey = (e: KeyboardEvent) => {
    const d = e.key === "ArrowDown" || e.key === "ArrowRight" ? 1 : e.key === "ArrowUp" || e.key === "ArrowLeft" ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const next = Math.max(0, Math.min(PAGES.length - 1, at + d));
    onPage(PAGES[next].id);
    rail.current?.querySelectorAll<HTMLElement>(".rail-item")[next]?.focus();
  };

  return (
    <nav ref={rail} className="rail" aria-label="Pages" onKeyDown={onKey} inert={hidden}>
      <span ref={(el) => void (el && (brackets.current[0] = el))} className="rail-bracket open" aria-hidden="true" />
      <span ref={(el) => void (el && (brackets.current[1] = el))} className="rail-bracket close" aria-hidden="true" />
      {PAGES.map((p, i) => (
        <button
          key={p.id}
          className={`rail-item${p.id === page ? " current" : ""}`}
          aria-current={p.id === page ? "page" : undefined}
          tabIndex={p.id === page ? 0 : -1}
          title={`${p.hint} (Alt+${i + 1})`}
          onClick={() => onPage(p.id)}
        >
          <span className="rail-face">
            <PageIcon page={p.id} />
            <span className="name">{p.label.toLowerCase()}</span>
          </span>
        </button>
      ))}
    </nav>
  );
}

/** Commander's Board drawer: open or closed, and its height when open. */
export type Drawer = { open: boolean; height: number; onChange: (open: boolean, height?: number) => void; peek?: ReactNode };

/**
 * One page as a section. In Navigator every page stays mounted in a column that slides to the
 * current one; the others are inert. In Commander its head can bring it to Navigator, and the
 * Board's head is a drawer handle: click it to open or close, drag it to any height.
 */
export function Panel({
  page,
  mode,
  current,
  at,
  onOpen,
  info,
  actions,
  drawer,
  children,
}: {
  page: Page;
  mode: Mode;
  current: boolean;
  /** The current page's place in the column (Navigator slides there). */
  at: number;
  onOpen: () => void;
  info?: ReactNode;
  /** Keys in the head, before Open (the page's Add). */
  actions?: ReactNode;
  /** Commander only: this page is a drawer under the others. */
  drawer?: Drawer;
  children: ReactNode;
}) {
  const label = PAGES.find((p) => p.id === page)!.label;
  const self = useRef<HTMLElement>(null);
  const drag = useRef<{ y: number; h: number; moved: boolean } | null>(null);
  const asDrawer = mode === "commander" && drawer ? drawer : undefined;
  const closed = asDrawer && !asDrawer.open;

  const onDown = (e: PointerEvent) => {
    if (!asDrawer || (e.target as HTMLElement).closest("button, input, select")) return;
    drag.current = { y: e.clientY, h: self.current?.offsetHeight ?? 0, moved: false };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onMove = (e: PointerEvent) => {
    const d = drag.current, el = self.current;
    if (!d || !el) return;
    const dy = d.y - e.clientY;
    if (!d.moved && Math.abs(dy) < 4) return;
    d.moved = true;
    const room = (el.parentElement?.clientHeight ?? 600) - 160;
    el.classList.add("resizing");
    el.style.height = `${Math.max(el.querySelector(".panel-head")!.clientHeight + 2, Math.min(room, d.h + dy))}px`;
  };
  const onUp = () => {
    const d = drag.current, el = self.current;
    drag.current = null;
    if (!d || !el || !asDrawer) return;
    if (!d.moved) return asDrawer.onChange(!asDrawer.open);
    const h = el.offsetHeight;
    el.classList.remove("resizing");
    el.style.height = "";
    if (h > 100) asDrawer.onChange(true, h);
    else asDrawer.onChange(false);
  };

  return (
    <section
      ref={self}
      className={`panel panel-${page}${current ? " current" : ""}${asDrawer ? " drawer" : ""}${closed ? " closed" : ""}`}
      inert={mode === "navigator" && !current}
      aria-label={label}
      style={{ "--i": index(page), "--at": at, ...(asDrawer ? { "--drawer-h": `${asDrawer.height}px` } : {}) } as CSSProperties}
    >
      <header className="panel-head" onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp} onPointerCancel={onUp}>
        {asDrawer && <span className="drawer-grip" aria-hidden="true" />}
        <h2>{label}</h2>
        {closed && asDrawer.peek ? <span className="panel-peek">{asDrawer.peek}</span> : null}
        <span className="panel-info lbl">{info}</span>
        {actions}
        {asDrawer && (
          <button
            className="key small drawer-toggle"
            aria-expanded={asDrawer.open}
            title={asDrawer.open ? `Close the ${label} (drag the head to resize)` : `Open the ${label}`}
            onClick={() => asDrawer.onChange(!asDrawer.open)}
          >
            <svg viewBox="0 0 14 14" aria-hidden="true">
              <path d="M3 9l4-4 4 4" />
            </svg>
          </button>
        )}
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
