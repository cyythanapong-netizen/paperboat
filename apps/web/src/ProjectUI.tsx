import { useRef } from "react";
import { useDismiss, useFocusReturn } from "@paperboat/shared/components";


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

