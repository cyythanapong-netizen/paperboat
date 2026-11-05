import { useEffect, useState } from "react";
import { Spinner } from "./Spinner";

type Toolkit = { name: string; repo: string; description: string };

const AVAILABLE: Toolkit[] = [
  { name: "opencv", repo: "https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv", description: "Image processing (OpenCV): filters, Image station object, image Dashboard instrument." },
];

async function fetchInstalled(): Promise<Set<string>> {
  try {
    const res = await fetch("/api/toolkits");
    if (!res.ok) return new Set();
    const data = (await res.json()) as { installed?: string[] };
    return new Set(data.installed ?? []);
  } catch {
    return new Set();
  }
}

/**
 * Top-bar control for optional toolkits. In a dev build the Install/Uninstall buttons
 * run the real installer through the Vite dev server; in a production build they fall
 * back to showing the manual command.
 */
export function Toolkits({ installedGroups }: { installedGroups: Set<string> }) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [installed, setInstalled] = useState<Set<string>>(installedGroups);

  useEffect(() => {
    fetchInstalled().then(setInstalled);
  }, []);

  const refresh = async () => setInstalled(await fetchInstalled());

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
      console.log(`[toolkit ${done}] ${name}\n${data.output ?? ""}`);
      setMessage(`${name} ${done}. Restart the backend to apply it.`);
      await refresh();
    } catch (err) {
      setMessage(`Could not ${done} ${name} in-app: ${err instanceof Error ? err.message : String(err)}.`);
    } finally {
      setBusy(null);
    }
  };

  const isInstalled = (name: string) => installed.has(name) || installedGroups.has(name);

  return (
    <span className="toolkits">
      <button className="key small" title="Optional toolkits" onClick={() => setOpen((v) => !v)}>
        Toolkits
      </button>
      {open && (
        <div className="toolkits-popover" role="menu">
          {AVAILABLE.map((t) => {
            const done = isInstalled(t.name);
            return (
              <div key={t.name} className="toolkits-row">
                <div className="toolkits-info">
                  <strong>{t.name}</strong>
                  <span className="muted">{done ? "installed" : "not installed"}</span>
                  <span className="toolkits-desc">{t.description}</span>
                </div>
                {done ? (
                  <>
                    <button className="key small" disabled={busy !== null} onClick={() => call("/api/install-toolkit", t.name, "reinstalled")}>
                      {busy === t.name ? <Spinner size={12} /> : "Reinstall"}
                    </button>
                    <button className="key small" disabled={busy !== null} onClick={() => call("/api/uninstall-toolkit", t.name, "uninstalled")}>
                      {busy === t.name ? <Spinner size={12} /> : "Uninstall"}
                    </button>
                  </>
                ) : (
                  <button className="key small" disabled={busy !== null} onClick={() => call("/api/install-toolkit", t.name, "installed")}>
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