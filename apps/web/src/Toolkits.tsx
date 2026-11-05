import { useState } from "react";
import { Spinner } from "./Spinner";

type Toolkit = { name: string; repo: string; description: string };

// Mirrors toolkits.toml at the repo root. Kept in sync by hand until the backend
// serves it (the install/compile itself happens out of band, see the CLI).
const AVAILABLE: Toolkit[] = [
  { name: "opencv", repo: "https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv", description: "Image processing (OpenCV): filters, Image station object, image Dashboard instrument." },
];

/**
 * Top-bar control for optional toolkits. Shows which are loaded (detected from the
 * backend's catalog) and lets the user trigger an install/update. Installing a C++
 * toolkit cannot happen inside the browser, so the action reports the CLI command and
 * shows a spinner while it (pretends to) work; the real build runs via
 * `python3 scripts/install_toolkit.py <name>` or `paperboat toolkit add <name>`.
 */
export function Toolkits({ installedGroups }: { installedGroups: Set<string> }) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);

  const call = async (endpoint: string, name: string, done: string) => {
    setBusy(name);
    setMessage(null);
    try {
      const res = await fetch(endpoint, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name }),
      });
      const data = (await res.json()) as { ok?: boolean; output?: string; error?: string };
      if (!res.ok || !data.ok) throw new Error(data.output || data.error || "request failed");
      setMessage(`${name} ${done}. Restart the backend to apply it.`);
    } catch (err) {
      setMessage(`Could not ${done} ${name} in-app: ${err instanceof Error ? err.message : String(err)}.`);
    } finally {
      setBusy(null);
    }
  };
  const install = (name: string) => call("/api/install-toolkit", name, "installed");
  const uninstall = (name: string) => call("/api/uninstall-toolkit", name, "uninstalled");

  return (
    <span className="toolkits">
      <button className="key small" title="Optional toolkits" onClick={() => setOpen((v) => !v)}>
        Toolkits
      </button>
      {open && (
        <div className="toolkits-popover" role="menu">
          {AVAILABLE.map((t) => {
            const installed = installedGroups.has(t.name);
            return (
              <div key={t.name} className="toolkits-row">
                <div className="toolkits-info">
                  <strong>{t.name}</strong>
                  <span className="muted">{installed ? "installed" : "not installed"}</span>
                  <span className="toolkits-desc">{t.description}</span>
                </div>
                {installed ? (
                  <>
                    <button className="key small" disabled={busy !== null} onClick={() => install(t.name)}>
                      {busy === t.name ? <Spinner size={12} /> : "Reinstall"}
                    </button>
                    <button className="key small" disabled={busy !== null} onClick={() => uninstall(t.name)}>
                      {busy === t.name ? <Spinner size={12} /> : "Uninstall"}
                    </button>
                  </>
                ) : (
                  <button className="key small" disabled={busy !== null} onClick={() => install(t.name)}>
                    {busy === t.name ? <Spinner size={12} /> : "Install"}
                  </button>
                )}
              </div>
            );
          })}
          {message && <div className="toolkits-msg">{message}</div>}
        </div>
      )}
    </span>
  );
}
