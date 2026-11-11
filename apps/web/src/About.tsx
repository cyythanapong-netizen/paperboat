import { useRef } from "react";
import { useDismiss, useFocusReturn } from "@paperboat/shared/components";
import { Logo } from "./Logo";
import type { EngineState } from "./engine";

const LICENSE = "https://github.com/paperboatlab/paperboat/blob/dev/LICENSE";
const CREATORS = [
  { name: "Thanapong Chuangyanyong", href: "https://github.com/thanapong-chuangyanyong" },
  { name: "Leonardo Bernazzoli", href: "https://github.com/LeoBernazzoli" },
];

/**
 * About Paperboat: the mark (its water rippling continuously, as on the bar) and what this
 * build is. Opens from the bar's About key or View > About; Esc, a press outside, or Close.
 */
export function AboutWindow({ state, onClose }: { state: EngineState; onClose: () => void }) {
  const root = useRef<HTMLElement>(null);
  useFocusReturn(root);
  useDismiss(root, onClose);

  const version = state.version || "unknown";
  const toolkits = state.toolkits.filter((t) => t.installed).sort((a, b) => a.name.localeCompare(b.name));

  return (
    <section className="console-window about-window" role="dialog" aria-label="About Paperboat" data-own-keys ref={root}>
      <header className="console-head">
        <h2>About Paperboat</h2>
        <span className="lbl">paperboat/0</span>
        <button className="key small" title="Close (Esc)" onClick={onClose}>
          Close
        </button>
      </header>
      <div className="about-body">
        <div className="about-mark">
          <Logo height={112} running />
          <span className="about-name">paperboat</span>
        </div>
        <div className="about-info">
          <dl className="about-facts">
            <dt>Version</dt>
            <dd className="mono">{version}</dd>
            <dt>Backend</dt>
            <dd className="mono">{state.engine || "Not connected"}</dd>
            <dt>Protocol</dt>
            <dd className="mono">paperboat/0</dd>
            <dt>License</dt>
            <dd>
              <a href={LICENSE} target="_blank" rel="noreferrer">
                Apache-2.0
              </a>
            </dd>
          </dl>
          <section className="about-section">
            <h3>Toolkits</h3>
            {toolkits.length ? (
              <ul className="about-list">
                {toolkits.map((t) => (
                  <li key={t.name}>
                    <span className="about-tool">{t.name}</span>
                    <span className="about-version">{t.version ?? "unknown"}</span>
                  </li>
                ))}
              </ul>
            ) : (
              <p className="about-empty">No optional toolkits installed.</p>
            )}
          </section>
          <section className="about-section">
            <h3>Creators</h3>
            <p className="about-creators">
              {CREATORS.map((c, i) => (
                <span key={c.name}>
                  <a href={c.href} target="_blank" rel="noreferrer">
                    {c.name}
                  </a>
                  {i < CREATORS.length - 1 ? " · " : ""}
                </span>
              ))}
            </p>
            <p className="about-org">
              A{" "}
              <a href="https://github.com/paperboatlab" target="_blank" rel="noreferrer">
                PaperboatLab
              </a>{" "}
              project.
            </p>
          </section>
        </div>
      </div>
    </section>
  );
}
