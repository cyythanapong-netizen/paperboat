import { useEffect, useRef, type RefObject } from "react";

/**
 * Focus for a window floating over the page: on open, its first button (the primary
 * `button.run` before the rest) when `root` is given; on close, back to where focus was.
 */
export function useFocusReturn(root?: RefObject<HTMLElement | null>) {
  useEffect(() => {
    const before = document.activeElement as HTMLElement | null;
    root?.current?.querySelector<HTMLElement>("button.run, button")?.focus();
    return () => {
      if (before?.isConnected) before.focus?.();
    };
  }, []);
}

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
  return (
    <section
      className="console-window notice"
      role="alertdialog"
      aria-label={spec.title}
      data-own-keys
      ref={root}
      onKeyDown={(e) => {
        if (e.key !== "Escape") return;
        e.preventDefault();
        e.stopPropagation();
        choose(spec.actions[spec.actions.length - 1]);
      }}
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

/** The project's name in the bar, with a dot while there are changes not saved. */
export function ProjectLabel({ name, dirty }: { name: string; dirty: boolean }) {
  return (
    <span className="proj" title={dirty ? `${name}: unsaved changes (Ctrl+S saves)` : name}>
      <span className="proj-name">{name}</span>
      {dirty && <i className="dirty" role="img" aria-label="Unsaved changes" />}
    </span>
  );
}
