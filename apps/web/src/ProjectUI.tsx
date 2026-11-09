import { useLayoutEffect, useRef } from "react";
import { useDismiss, useFocusReturn } from "@paperboat/shared/components";

export { useFocusReturn };

/** A question or a message for the user, and what they can do about it. */
export type NoticeSpec = {
  title: string;
  /** Paragraphs. */
  text: string[];
  /** The last one is what Esc does (usually Cancel or OK). The primary one has focus. */
  actions: { label: string; primary?: boolean; run?: () => void }[];
};

/**
 * A notice floating in the middle of the screen: opening a project that needs a word first (another
 * backend, unsaved changes, a file that can't be read), or the offer to restore the last session. Not
 * modal: nothing behind it is blocked, but it keeps its keys (Enter, Esc) to itself.
 */
export function Notice({ spec, onClose }: { spec: NoticeSpec; onClose: () => void }) {
  const root = useRef<HTMLElement>(null);
  const choose = (a: NoticeSpec["actions"][number]) => {
    onClose();
    a.run?.();
  };
  useFocusReturn(root);
  useDismiss(root, () => choose(spec.actions[spec.actions.length - 1]), { outside: false });
  return (
    <section
      className="console-window notice"
      role="alertdialog"
      aria-label={spec.title}
      data-own-keys
      ref={root}
    >
      <header className="console-head">
        <h2>{spec.title}</h2>
      </header>
      <div className="notice-body">
        {spec.text.map((t, i) => (
          <p key={i}>{t}</p>
        ))}
      </div>
      <footer className="notice-actions">
        {spec.actions.map((a) => (
          <button key={a.label} className={`key${a.primary ? " run" : ""}`} onClick={() => choose(a)}>
            {a.label}
          </button>
        ))}
      </footer>
    </section>
  );
}

/** Whether a flex row has wrapped: some child starts below where another ends. */
function wrapped(row: HTMLElement): boolean {
  let top = -Infinity, bottom = Infinity;
  for (const c of row.children) {
    const r = c.getBoundingClientRect();
    if (r.height === 0) continue; // nothing drawn (the spacer)
    top = Math.max(top, r.top);
    bottom = Math.min(bottom, r.bottom);
  }
  return top >= bottom;
}

/**
 * The project's name in the bar, with a dot while there are changes not saved. It sits in the bar's flow
 * (after the menus, before the bar's spacer) and is pushed right toward the bar's centre, but never by more
 * than the spacer's room: centred while the bar has space, then sliding toward the menus, then truncating,
 * and never under the keys on the right. Once the bar wraps onto two lines it simply follows the menus.
 */
export function ProjectLabel({ name, dirty }: { name: string; dirty: boolean }) {
  const ref = useRef<HTMLSpanElement>(null);
  useLayoutEffect(() => {
    const el = ref.current, bar = el?.parentElement;
    if (!el || !bar) return;
    const place = () => {
      el.style.marginLeft = ""; // measure from where the flow puts it
      const spacer = bar.querySelector<HTMLElement>(":scope > .spacer");
      if (!spacer || wrapped(bar)) return;
      const b = bar.getBoundingClientRect(), r = el.getBoundingClientRect();
      const shift = Math.min(b.left + b.width / 2 - r.width / 2 - r.left, spacer.getBoundingClientRect().width);
      if (shift >= 1) el.style.marginLeft = `${Math.floor(shift)}px`;
    };
    // Whatever changes the room: the window, the name, a key's label (errors, the backend's name, a lock).
    // Not the spacer, which only follows the shift made here.
    const ro = new ResizeObserver(place);
    const watch = () => {
      ro.disconnect();
      ro.observe(bar);
      for (const c of bar.children) if (!c.classList.contains("spacer")) ro.observe(c);
      place();
    };
    watch();
    const mo = new MutationObserver(watch); // a key that comes or goes
    mo.observe(bar, { childList: true });
    return () => {
      ro.disconnect();
      mo.disconnect();
      el.style.marginLeft = "";
    };
  }, []);
  return (
    <span ref={ref} className="proj" title={dirty ? `${name}: unsaved changes (Ctrl+S saves)` : name}>
      <span className="proj-name">{name}</span>
      {dirty && <i className="dirty" role="img" aria-label="Unsaved changes" />}
    </span>
  );
}
