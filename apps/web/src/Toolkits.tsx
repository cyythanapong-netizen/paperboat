import { useEffect, useRef, useState } from "react";
import { DEFAULT_TOOLKITS, missingRequiredToolkits } from "./engine";
import type { ToolkitStatus } from "./protocol";
import { Spinner } from "./Spinner";

export { DEFAULT_TOOLKITS, missingRequiredToolkits };

export function ToolkitsButton({
  toolkits,
  connected,
  onInstall,
  onUpdate,
  onUninstall,
  onRestart,
}: {
  toolkits: ToolkitStatus[];
  connected: boolean;
  onInstall: (name: string) => void;
  onUpdate: (name: string) => void;
  onUninstall: (name: string) => void;
  onRestart: () => void;
}) {
  const [open, setOpen] = useState(false);
  const wrap = useRef<HTMLDivElement | null>(null);
  const items = toolkits.length > 0 ? toolkits : DEFAULT_TOOLKITS;
  const anyBusy = items.some((t) => !!t.busy);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (wrap.current && !wrap.current.contains(e.target as Node)) setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") setOpen(false);
    };
    window.addEventListener("mousedown", onDown);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("mousedown", onDown);
      window.removeEventListener("keydown", onKey);
    };
  }, [open]);

  return (
    <div ref={wrap} className="toolkits-wrap">
      <button
        className={`key small${open ? " on" : ""}`}
        aria-expanded={open}
        title="Optional toolkits: install, update, or uninstall"
        onClick={() => setOpen(!open)}
      >
        {anyBusy && <Spinner label="Toolkit operation in progress" />}
        Toolkits
      </button>
      {open && (
        <section className="pop toolkits-pop" role="dialog" aria-label="Toolkits">
          <div className="head">Optional toolkits</div>
          {items.map((t) => {
            const busy = t.busy;
            const statusText = busy
              ? `${busy}…`
              : t.installed
                ? "installed"
                : t.required
                  ? "missing (required)"
                  : "not installed";
            return (
              <div key={t.name} className="toolkit-item">
                <div className="toolkit-head">
                  <code>{t.name}</code>
                  <span className={`toolkit-status${t.installed ? " on" : ""}${t.required && !t.installed ? " req" : ""}`}>
                    {busy && <Spinner label={statusText} />}
                    {statusText}
                  </span>
                </div>
                {t.description && <p className="toolkit-desc">{t.description}</p>}
                <div className="toolkit-actions">
                  <a className="toolkit-repo" href={t.repo.replace(/\.git$/, "")} target="_blank" rel="noreferrer">
                    Repository
                  </a>
                  <span className="spacer" />
                  {!t.installed ? (
                    <button
                      className="key small"
                      disabled={!connected || !!busy}
                      title={connected ? `Install ${t.name} and rebuild the engine` : "Connect to a backend first"}
                      onClick={() => onInstall(t.name)}
                    >
                      {busy === "installing" ? (
                        <>
                          <Spinner /> Installing…
                        </>
                      ) : (
                        "Install"
                      )}
                    </button>
                  ) : (
                    <>
                      <button
                        className="key small"
                        disabled={!connected || !!busy}
                        title={`Pull latest ${t.name} changes and rebuild`}
                        onClick={() => onUpdate(t.name)}
                      >
                        {busy === "updating" ? (
                          <>
                            <Spinner /> Updating…
                          </>
                        ) : (
                          "Update"
                        )}
                      </button>
                      <button
                        className="key small"
                        disabled={!connected || !!busy}
                        title={`Remove ${t.name} and rebuild`}
                        onClick={() => onUninstall(t.name)}
                      >
                        {busy === "uninstalling" ? (
                          <>
                            <Spinner /> Uninstalling…
                          </>
                        ) : (
                          "Uninstall"
                        )}
                      </button>
                    </>
                  )}
                </div>
              </div>
            );
          })}
          <div className="foot toolkits-foot">
            <span>Restart the backend after installing or updating.</span>
            <button
              className="key small"
              disabled={!connected || anyBusy}
              title="Restart the connected backend process"
              onClick={() => {
                setOpen(false);
                onRestart();
              }}
            >
              Restart backend
            </button>
          </div>
        </section>
      )}
    </div>
  );
}
