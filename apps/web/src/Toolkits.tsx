import { useEffect, useRef, useState } from "react";
import { useDismiss } from "@paperboat/shared/components";
import { missingRequiredToolkits } from "./engine";
import type { ToolkitStatus } from "./protocol";
import { useFocusReturn } from "./ProjectUI";
import { Spinner } from "./Spinner";

export { missingRequiredToolkits };

export function MissingToolkitsDialog({
  missing,
  onInstall,
  onCancel,
}: {
  missing: ToolkitStatus[];
  onInstall: (selections: { name: string; version: string }[]) => void;
  onCancel: () => void;
}) {
  const root = useRef<HTMLElement>(null);
  const [selectedVersions, setSelectedVersions] = useState<Record<string, string>>(() => {
    const init: Record<string, string> = {};
    for (const t of missing) {
      init[t.name] = t.default ?? (t.versions && t.versions.length > 0 ? t.versions[0] : "dev");
    }
    return init;
  });

  useFocusReturn(root);
  useDismiss(root, onCancel, { outside: false });

  return (
    <section
      className="console-window notice missing-toolkits-dialog"
      role="alertdialog"
      aria-label="Install missing toolkits"
      data-own-keys
      ref={root}
    >
      <header className="console-head">
        <h2>{missing.length === 1 ? `Install missing toolkit “${missing[0].name}”?` : "Install missing toolkits?"}</h2>
      </header>
      <div className="notice-body">
        <p>
          This backend or project requires {missing.length === 1 ? "a toolkit" : "toolkits"} that{" "}
          {missing.length === 1 ? "is" : "are"} not installed.
        </p>
        <div style={{ display: "flex", flexDirection: "column", gap: "var(--space-2, 8px)", margin: "var(--space-2, 8px) 0" }}>
          {missing.map((t) => {
            const versions = t.versions ?? [];
            const cur = selectedVersions[t.name] ?? t.default ?? (versions.length > 0 ? versions[0] : "dev");
            return (
              <div
                key={t.name}
                className="toolkit-item"
                style={{
                  border: "1px solid var(--line)",
                  borderRadius: "var(--radius-md, 6px)",
                  padding: "var(--space-2, 8px)",
                }}
              >
                <div
                  className="toolkit-head"
                  style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: "var(--space-2, 8px)" }}
                >
                  <code>{t.name}</code>
                  <div style={{ display: "flex", alignItems: "center", gap: "6px" }}>
                    <label htmlFor={`ver-${t.name}`} style={{ fontSize: "var(--size-2xs, 11px)", color: "var(--text-muted)" }}>
                      Version:
                    </label>
                    <select
                      id={`ver-${t.name}`}
                      className="toolkit-version-select"
                      value={cur}
                      onChange={(e) => setSelectedVersions((prev) => ({ ...prev, [t.name]: e.target.value }))}
                    >
                      {versions.map((v) => (
                        <option key={v} value={v}>
                          {v}
                        </option>
                      ))}
                      <option value="dev">dev (main)</option>
                    </select>
                  </div>
                </div>
                {t.description && <p className="toolkit-desc">{t.description}</p>}
                <div style={{ marginTop: "4px" }}>
                  <a className="toolkit-repo" href={t.repo.replace(/\.git$/, "")} target="_blank" rel="noreferrer">
                    Repository
                  </a>
                </div>
              </div>
            );
          })}
        </div>
      </div>
      <footer className="notice-actions">
        <button
          className="key run"
          onClick={() => {
            const list = missing.map((t) => ({
              name: t.name,
              version: selectedVersions[t.name] ?? t.default ?? (t.versions && t.versions.length > 0 ? t.versions[0] : "dev"),
            }));
            onInstall(list);
          }}
        >
          {missing.length === 1 ? `Install ${missing[0].name}` : "Install all"}
        </button>
        <button className="key" onClick={onCancel}>
          Not now
        </button>
      </footer>
    </section>
  );
}

export function ToolkitsButton({
  toolkits,
  connected,
  onInstall,
  onUninstall,
  onRestart,
  onVersions,
}: {
  toolkits: ToolkitStatus[];
  connected: boolean;
  onInstall: (name: string, version?: string) => void;
  onUninstall: (name: string) => void;
  onRestart: () => void;
  onVersions?: (name: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const [selectedVersions, setSelectedVersions] = useState<Record<string, string>>({});
  const wrap = useRef<HTMLDivElement | null>(null);
  const items = toolkits;
  const anyBusy = items.some((t) => !!t.busy);

  useDismiss(wrap, () => setOpen(false), { enabled: open });

  useEffect(() => {
    if (!open) return;
    for (const t of items) {
      if (!t.versions || t.versions.length === 0) {
        onVersions?.(t.name);
      }
    }
  }, [open, items, onVersions]);

  return (
    <div ref={wrap} className="toolkits-wrap">
      <button
        className={`key small${open ? " on" : ""}`}
        aria-expanded={open}
        title="Toolkits: install, update, or uninstall"
        onClick={() => setOpen(!open)}
      >
        {anyBusy && <Spinner label="Toolkit operation in progress" />}
        Toolkits
      </button>
      {open && (
        <section className="pop toolkits-pop" role="dialog" aria-label="Toolkits">
          <div className="head">Toolkits</div>
          {items.length === 0 ? (
            <p className="toolkit-desc" style={{ padding: "8px 12px", margin: 0 }}>
              No optional toolkits registered.
            </p>
          ) : (
            items.map((t) => {
            const busy = t.busy;
            const versions = t.versions ?? [];
            const curSelection =
              selectedVersions[t.name] ??
              t.version ??
              t.default ??
              (versions.length > 0 ? versions[0] : "dev");
            const statusText = busy
              ? `${busy}…`
              : t.installed
                ? t.version
                  ? `installed ${t.version}`
                  : "installed"
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
                  <select
                    className="toolkit-version-select"
                    value={curSelection}
                    disabled={!connected || !!busy}
                    onChange={(e) => setSelectedVersions((prev) => ({ ...prev, [t.name]: e.target.value }))}
                  >
                    {versions.map((v) => (
                      <option key={v} value={v}>
                        {v}
                      </option>
                    ))}
                    <option value="dev">dev (main)</option>
                  </select>
                  {!t.installed ? (
                    <button
                      className="key small"
                      disabled={!connected || !!busy}
                      title={connected ? `Install ${t.name} (${curSelection}) and rebuild` : "Connect to a backend first"}
                      onClick={() => onInstall(t.name, curSelection)}
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
                        disabled={!connected || !!busy || curSelection === t.version}
                        title={
                          curSelection === t.version
                            ? `Version ${curSelection} already installed`
                            : `Switch to ${curSelection} and rebuild`
                        }
                        onClick={() => onInstall(t.name, curSelection)}
                      >
                        {busy === "installing" ? (
                          <>
                            <Spinner /> Installing…
                          </>
                        ) : (
                          "Install selected"
                        )}
                      </button>
                      <button
                        className="key small"
                        disabled={!connected || !!busy || !!t.builtin || !!t.required}
                        title={
                          t.builtin
                            ? "Builtin: it ships with the engine"
                            : t.required
                              ? "Required by this backend: it can't be removed"
                              : `Remove ${t.name} and rebuild`
                        }
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
          }))}
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
