import { useEffect, useRef, useState } from "react";
import { useDismiss } from "@paperboat/shared/components";
import { PackagePanel } from "@paperboat/shared/packages";
import { missingRequiredToolkits } from "./engine";
import type { ToolkitStatus } from "./protocol";
import { useFocusReturn } from "./ProjectUI";
import { Spinner } from "./Spinner";

export { missingRequiredToolkits };

export function MissingToolkitsDialog({
  missing,
  onCancel,
}: {
  missing: ToolkitStatus[];
  onCancel: () => void;
}) {
  const root = useRef<HTMLElement>(null);
  const [copied, setCopied] = useState(false);

  useFocusReturn(root);
  useDismiss(root, onCancel, { outside: false });

  const commands = missing.map((t) => {
    const source = t.repo ? `--repo ${t.repo}` : "--repo <url>";
    return `paperboat toolkit install ${t.name} ${source}`;
  });
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(commands.join("\n"));
      setCopied(true);
    } catch {
      setCopied(false);
    }
  };

  return (
    <section
      className="console-window notice missing-toolkits-dialog"
      role="alertdialog"
      aria-label="Missing toolkits"
      data-own-keys
      ref={root}
    >
      <header className="console-head">
        <h2>{missing.length === 1 ? `Missing toolkit “${missing[0].name}”` : "Missing toolkits"}</h2>
      </header>
      <div className="notice-body">
        <p>
          This backend or project requires {missing.length === 1 ? "a toolkit" : "toolkits"} that{" "}
          {missing.length === 1 ? "is" : "are"} not installed. Install from the repository or folder you trust;
          the manifest only suggests a source.
        </p>
        <div style={{ display: "flex", flexDirection: "column", gap: "var(--space-2, 8px)", margin: "var(--space-2, 8px) 0" }}>
          {missing.map((t, i) => (
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
                {t.repo && (
                  <a className="toolkit-repo" href={t.repo.replace(/\.git$/, "")} target="_blank" rel="noreferrer">
                    Suggested repository
                  </a>
                )}
              </div>
              {t.description && <p className="toolkit-desc">{t.description}</p>}
              <pre
                style={{
                  margin: "6px 0 0",
                  padding: "6px 8px",
                  background: "var(--surface-well)",
                  borderRadius: "var(--radius-sm, 3px)",
                  whiteSpace: "pre-wrap",
                  wordBreak: "break-all",
                  fontSize: "var(--size-2xs, 11px)",
                }}
              >
                <code>{commands[i]}</code>
              </pre>
            </div>
          ))}
        </div>
      </div>
      <footer className="notice-actions">
        <button className="key run" onClick={copy}>
          {copied ? "Copied" : "Copy commands"}
        </button>
        <button className="key" onClick={onCancel}>
          Close
        </button>
      </footer>
    </section>
  );
}

/**
 * The Toolkits key and its panel: the shared package panel (@paperboat/shared/packages), as
 * Squero MX shows Julia and Squero Studio OM its Modelica libraries. Paperboat is the runtime,
 * the installed toolkits the environment, `toolkits.toml` the registry.
 */
export function ToolkitsButton({
  toolkits,
  connected,
  requested,
  version,
  backend,
  onInstall,
  onUninstall,
  onRestart,
  onVersions,
}: {
  toolkits: ToolkitStatus[];
  connected: boolean;
  /** What was asked for and not yet acknowledged (ink installs, web sends), so the row says so at once. */
  requested: Record<string, string>;
  /** The Paperboat build serving the backend (hello.version). */
  version?: string;
  /** The backend's name (hello.engine). */
  backend?: string;
  onInstall: (name: string, version?: string) => void;
  onUninstall: (name: string) => void;
  onRestart: () => void;
  onVersions?: (name: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const wrap = useRef<HTMLDivElement | null>(null);
  const items = toolkits.filter((t) => !t.builtin);
  const busyOf = (t: ToolkitStatus) => t.busy ?? requested[t.name];
  const working = items.find((t) => !!busyOf(t));
  const busy = working ? `${busyOf(working)} ${working.name}` : "";

  useDismiss(wrap, () => setOpen(false), { enabled: open });

  useEffect(() => {
    if (!open) return;
    for (const t of items) if (!t.versions || t.versions.length === 0) onVersions?.(t.name);
  }, [open, items, onVersions]);

  const versionOf = (t: ToolkitStatus) => (t.version && t.version !== "unknown" ? t.version : undefined);
  return (
    <div ref={wrap} className="toolkits-wrap">
      <button className={`key small${open ? " on" : ""}`} aria-expanded={open} title="Toolkits: install, update, or uninstall" onClick={() => setOpen(!open)}>
        {working && <Spinner label="Toolkit operation in progress" />}
        Toolkits
      </button>
      {open && (
        <section className="pop toolkits-pop" role="dialog" aria-label="Toolkits">
          <PackagePanel
            noun="toolkit"
            busy={busy}
            error={connected ? undefined : "Not connected to a backend: connect one to install toolkits."}
            runtime={{
              title: "paperboat",
              tag: version,
              hint: "Update Paperboat itself with `paperboat update`; toolkits install into the backend it serves.",
            }}
            environment={{
              title: "toolkits",
              tag: backend,
              packages: items
                .filter((t) => t.installed || busyOf(t))
                .map((t) => ({
                  name: t.name,
                  version: versionOf(t),
                  status: busyOf(t) ? `${busyOf(t)}…` : undefined,
                  keep: t.required ? "Required by this backend: it can't be removed" : undefined,
                })),
              empty: "No optional toolkits installed. Add one from the registry.",
              remove: connected ? (name) => onUninstall(name) : undefined,
              actions: [{ label: "Restart backend", title: "Restart the backend after installing or updating", disabled: !connected, run: () => (setOpen(false), onRestart()) }],
            }}
            registry={{
              title: "toolkits.toml",
              items: items.map((t) => ({
                name: t.name,
                latest: versionOf(t) ?? t.default ?? t.versions?.[0],
                description: t.description,
                repo: t.repo,
                versions: [...(t.versions ?? []), "dev"],
                facts: t.required && !t.installed ? ["missing: required by this backend"] : undefined,
              })),
              itemsLabel: `${items.length} toolkit${items.length === 1 ? "" : "s"}`,
              pickVersion: true,
              add: (name, v) => connected && onInstall(name, v),
            }}
          />
        </section>
      )}
    </div>
  );
}
