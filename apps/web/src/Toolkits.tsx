import { useEffect, useState } from "react";
import type { ClientMsg } from "./protocol";
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
 * Top-bar control for optional toolkits. Installing runs in the backend, so its progress
 * and result appear in Paperboat's Console; the button only starts it and shows a spinner
 * until the backend reports that it is done.
 */
export function Toolkits({ installedGroups, send, logs }: { installedGroups: Set<string>; send: (m: ClientMsg) => void; logs: { level: string; text: string }[] }) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [installed, setInstalled] = useState<Set<string>>(installedGroups);

  useEffect(() => {
    fetchInstalled().then(setInstalled);
  }, []);

  useEffect(() => {
    if (!busy) return;
    const last = logs[logs.length - 1];
    if (!last) return;
    if (!last.text.includes(busy)) return;
    if (last.text.includes("installed") || last.text.includes("uninstalled") || last.text.includes("failed")) {
      setBusy(null);
      setMessage(last.text);
      fetchInstalled().then(setInstalled);
    }
  }, [logs, busy]);

  const install = (name: string) => {
    setBusy(name);
    setMessage(null);
    send({ type: "toolkit.install", name });
  };

  const uninstall = (name: string) => {
    setBusy(name);
    setMessage(null);
    send({ type: "toolkit.uninstall", name });
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