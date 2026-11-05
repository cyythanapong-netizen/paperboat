import { useLayoutEffect, useRef, type CSSProperties, type KeyboardEvent, type PointerEvent, type ReactNode } from "react";
import { Logo } from "./Logo";

/**
 * The app's frame (apps/web/DESIGN.md): two view modes.
 * - Navigator: one page at a time, picked on the rail (Alt+1–3). The pages sit in a column and
 *   the column slides to the one picked. The default.
 * - Commander: Station and Flow side by side, the Board a drawer under them.
 * - Lookout: the Board, for whoever runs and watches the rig; Station and Flow are drawers at
 *   its left and right, closed until wanted.
 * The Board is the `dashboard` page in code. The Console is not a page: it floats over them.
 */
export type Page = "station" | "flow" | "dashboard";
export type Mode = "navigator" | "commander" | "lookout";
export const MODES: Mode[] = ["navigator", "commander", "lookout"];
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

/** The mark and the name. `running` ripples the water (W2) while a run is live. */
export function Wordmark({ running = false }: { running?: boolean }) {
  return (
    <span className="wordmark">
      <Logo height={24} running={running} />
      paperboat
    </span>
  );
}

/** The chosen option in a bracketed choice: a switch's `.opt.on`, or a tab's `[aria-selected]`. */
const CHOSEN = ".opt.on, .opt[aria-checked='true'], .opt[aria-selected='true']";

/**
 * The brackets of a bracketed choice (`[ navigator ]  commander`), drawn once for the whole
 * group so they glide to a new choice instead of blinking there: the same motion as the rail's.
 * Put it inside the group; the group gets `glide` (its options' own bracket glyphs only keep
 * their room, so nothing shifts). The options are found by `CHOSEN`, and watched, so a choice
 * made anywhere (a click, a key, a command) moves the brackets.
 */
export function GlideBrackets() {
  const open = useRef<HTMLSpanElement>(null);
  const close = useRef<HTMLSpanElement>(null);
  useLayoutEffect(() => {
    const a = open.current, b = close.current, host = a?.parentElement;
    if (!a || !b || !host) return;
    host.classList.add("glide");
    let shown = false;
    const place = (animate: boolean) => {
      const on = host.querySelector<HTMLElement>(CHOSEN);
      if (!on || !on.offsetParent) {
        a.style.opacity = b.style.opacity = "0";
        shown = false;
        return;
      }
      const hb = host.getBoundingClientRect(), ob = on.getBoundingClientRect(), cs = getComputedStyle(on);
      // The option's own glyphs sit just inside its padding: draw ours exactly there.
      for (const el of [a, b]) {
        el.style.font = cs.font;
        el.style.color = cs.color;
        el.style.top = `${ob.top - hb.top + host.scrollTop}px`;
        el.style.height = `${ob.height}px`;
      }
      const left = ob.left - hb.left + host.scrollLeft + parseFloat(cs.paddingLeft);
      const right = ob.right - hb.left + host.scrollLeft - parseFloat(cs.paddingRight) - b.offsetWidth;
      const glide = animate && shown && !reduceMotion();
      for (const el of [a, b]) el.style.transition = glide ? "left var(--glide), color var(--glide)" : "none";
      a.style.left = `${left}px`;
      b.style.left = `${right}px`;
      a.style.opacity = b.style.opacity = "1";
      shown = true;
    };
    place(false);
    // Measured in the fallback font before the web font arrives: measure again once it has.
    let live = true;
    void document.fonts?.ready.then(() => live && place(false));
    const mo = new MutationObserver(() => place(true));
    mo.observe(host, { subtree: true, attributes: true, attributeFilter: ["class", "aria-checked", "aria-selected"], childList: true, characterData: true });
    let started = false;
    const ro = new ResizeObserver(() => (started ? place(false) : (started = true)));
    ro.observe(host);
    return () => {
      live = false;
      mo.disconnect();
      ro.disconnect();
      host.classList.remove("glide");
    };
  }, []);
  return (
    <>
      <span ref={open} className="glide-bracket" aria-hidden="true">
        [
      </span>
      <span ref={close} className="glide-bracket" aria-hidden="true">
        ]
      </span>
    </>
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
      <GlideBrackets />
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

/**
 * A page as a drawer: Commander's Board under the others, Lookout's Station and Flow at the
 * Board's sides. Open or closed, and its size (height, or width at a side) when open.
 */
export type Drawer = {
  side: "bottom" | "left" | "right";
  open: boolean;
  size: number;
  onChange: (open: boolean, size?: number) => void;
  /** Shown in the head while closed (a few live readouts). */
  peek?: ReactNode;
};

/** A drawer is never dragged smaller than this; let go below it and it closes. */
const DRAWER_MIN = 120;

/**
 * One page as a section. In Navigator every page stays mounted in a column that slides to the
 * current one; the others are inert. Elsewhere its head can bring it to Navigator. As a drawer
 * its head is the handle: click it to open or close, drag it (or a side drawer's inner edge)
 * to any size. A side drawer closes to a tab: its icon and name, standing up.
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
  /** This page is a drawer in the current mode. */
  drawer?: Drawer;
  children: ReactNode;
}) {
  const label = PAGES.find((p) => p.id === page)!.label;
  const self = useRef<HTMLElement>(null);
  const drag = useRef<{ x: number; y: number; size: number; moved: boolean } | null>(null);
  const closed = drawer && !drawer.open;
  const side = drawer && drawer.side !== "bottom";

  const onDown = (e: PointerEvent) => {
    if (!drawer || e.button !== 0 || (e.target as HTMLElement).closest("button, input, select, a")) return;
    const el = self.current!;
    drag.current = { x: e.clientX, y: e.clientY, size: side ? el.offsetWidth : el.offsetHeight, moved: false };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onMove = (e: PointerEvent) => {
    const d = drag.current, el = self.current;
    if (!d || !el || !drawer) return;
    const delta = drawer.side === "bottom" ? d.y - e.clientY : drawer.side === "left" ? e.clientX - d.x : d.x - e.clientX;
    if (!d.moved && Math.abs(delta) < 4) return;
    d.moved = true;
    // Room is measured on the pages, not the panel's parent: in Commander and Lookout that is the
    // page stack, which has no box of its own (display: contents).
    const pages = el.closest(".pages") as HTMLElement | null;
    const room = side ? (pages?.clientWidth ?? 1200) - 360 : (pages?.clientHeight ?? 600) - 160;
    // Never smaller than closed: a head's height, or a side tab's width (the same).
    const least = (parseFloat(getComputedStyle(el).getPropertyValue("--panel-head-height")) || 36) + 2;
    el.classList.add("resizing");
    el.style.setProperty(side ? "width" : "height", `${Math.max(least, Math.min(room, d.size + delta))}px`);
  };
  const onUp = () => {
    const d = drag.current, el = self.current;
    drag.current = null;
    if (!d || !el || !drawer) return;
    if (!d.moved) return drawer.onChange(!drawer.open);
    const size = side ? el.offsetWidth : el.offsetHeight;
    el.classList.remove("resizing");
    el.style.removeProperty(side ? "width" : "height");
    if (size >= DRAWER_MIN) drawer.onChange(true, size);
    else drawer.onChange(false);
  };
  const handle = drawer ? { onPointerDown: onDown, onPointerMove: onMove, onPointerUp: onUp, onPointerCancel: onUp } : {};
  const toggle = drawer && (
    <button
      className={`key small drawer-toggle side-${drawer.side}`}
      aria-expanded={drawer.open}
      title={drawer.open ? `Close the ${label} (drag the head to resize)` : `Open the ${label}`}
      onClick={() => drawer.onChange(!drawer.open)}
    >
      <svg viewBox="0 0 14 14" aria-hidden="true">
        <path d="M3 9l4-4 4 4" />
      </svg>
    </button>
  );

  return (
    <section
      ref={self}
      className={`panel panel-${page}${current ? " current" : ""}${drawer ? ` drawer drawer-${drawer.side}` : ""}${closed ? " closed" : ""}`}
      inert={mode === "navigator" && !current}
      aria-label={label}
      style={{ "--i": index(page), "--at": at, ...(drawer ? { "--drawer-size": `${drawer.size}px` } : {}) } as CSSProperties}
    >
      {closed && side ? (
        <header className="panel-head side-tab" {...handle} title={`Open the ${label}`}>
          {toggle}
          <PageIcon page={page} />
          <h2>{label}</h2>
        </header>
      ) : (
        <header className="panel-head" {...handle}>
          {drawer?.side === "bottom" && <span className="drawer-grip" aria-hidden="true" />}
          {drawer?.side === "right" && toggle}
          <h2>{label}</h2>
          {closed && drawer.peek ? <span className="panel-peek">{drawer.peek}</span> : null}
          <span className="panel-info lbl">{info}</span>
          {actions}
          {drawer && drawer.side !== "right" && toggle}
          {mode !== "navigator" && (
            <button className="key small" title={`Show ${label} on its own (Navigator)`} onClick={onOpen}>
              Open
            </button>
          )}
        </header>
      )}
      {side && !closed && <span className="drawer-edge" aria-hidden="true" {...handle} />}
      <div className="panel-body">{children}</div>
    </section>
  );
}
