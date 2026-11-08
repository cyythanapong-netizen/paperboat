import { useRef, useState } from "react";
import { generateSalt, hashPasscode, verifyPasscode, type LockConfig } from "./lock";
import { useFocusReturn } from "./ProjectUI";

export function LockDialog({
  lock,
  unlocked,
  onUnlock,
  onSave,
  onClose,
}: {
  lock: LockConfig | null | undefined;
  unlocked: boolean;
  onUnlock: () => void;
  onSave: (config: LockConfig | null) => void;
  onClose: () => void;
}) {
  const root = useRef<HTMLElement>(null);
  useFocusReturn(root);

  const isLocked = !!lock && !unlocked;

  // Unlock mode state
  const [passcodeAttempt, setPasscodeAttempt] = useState("");
  const [unlockError, setUnlockError] = useState("");

  // Configure mode state
  const [lockArrange, setLockArrange] = useState(lock?.arrange ?? true);
  const [pages, setPages] = useState<string[]>(lock?.pages ?? ["station", "flow", "dashboard"]);
  const [modes, setModes] = useState<string[]>(lock?.modes ?? ["navigator", "commander", "lookout"]);
  const [newPasscode, setNewPasscode] = useState("");
  const [confirmPasscode, setConfirmPasscode] = useState("");
  const [configError, setConfigError] = useState("");

  const handleUnlock = async (e: React.FormEvent) => {
    e.preventDefault();
    setUnlockError("");
    if (!lock?.passcode) {
      onUnlock();
      return;
    }
    const ok = await verifyPasscode(passcodeAttempt, lock.passcode);
    if (ok) {
      onUnlock();
    } else {
      setUnlockError("Incorrect passcode.");
    }
  };

  const togglePage = (id: string) => {
    setPages((prev) => (prev.includes(id) ? (prev.length > 1 ? prev.filter((p) => p !== id) : prev) : [...prev, id]));
  };

  const toggleMode = (id: string) => {
    setModes((prev) => (prev.includes(id) ? (prev.length > 1 ? prev.filter((m) => m !== id) : prev) : [...prev, id]));
  };

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    setConfigError("");

    if (newPasscode) {
      if (newPasscode !== confirmPasscode) {
        setConfigError("Passcodes do not match.");
        return;
      }
    }

    let passcodeObj = lock?.passcode;
    if (newPasscode) {
      const salt = generateSalt();
      const hash = await hashPasscode(newPasscode, salt);
      passcodeObj = { algo: "sha256", salt, hash };
    }

    const cfg: LockConfig = {
      arrange: lockArrange,
      pages: pages.length === 3 ? undefined : pages,
      modes: modes.length === 3 ? undefined : modes,
      passcode: passcodeObj,
    };

    onSave(cfg);
    onClose();
  };

  const handleClear = () => {
    onSave(null);
    onClose();
  };

  return (
    <section
      className="console-window notice lock-dialog"
      role="dialog"
      aria-label={isLocked ? "Unlock Project UI" : "Project UI Lock"}
      data-own-keys
      ref={root}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          onClose();
        }
      }}
      style={{ maxWidth: 440 }}
    >
      <header className="console-head">
        <h2>{isLocked ? "Unlock Project UI" : "Project UI Lock Settings"}</h2>
        <span className="spacer" />
        <button className="key small" onClick={onClose} aria-label="Close">✕</button>
      </header>

      <div className="notice-body" style={{ padding: "16px 20px" }}>
        {isLocked ? (
          <form onSubmit={handleUnlock}>
            <p style={{ margin: "0 0 12px", color: "var(--text-muted)" }}>
              Parts of the interface are locked by this project. Enter the passcode to unlock them for this session.
            </p>
            {lock?.passcode ? (
              <div style={{ marginBottom: 12 }}>
                <label style={{ display: "block", marginBottom: 6, fontSize: 13, fontWeight: 500 }}>Passcode</label>
                <input
                  type="password"
                  autoFocus
                  className="input-text"
                  value={passcodeAttempt}
                  onChange={(e) => setPasscodeAttempt(e.target.value)}
                  placeholder="Enter passcode"
                  style={{ width: "100%", padding: "6px 8px" }}
                />
              </div>
            ) : (
              <p style={{ margin: "0 0 12px", fontStyle: "italic", color: "var(--text-muted)" }}>
                This project has no passcode set. Click Unlock to restore full controls.
              </p>
            )}
            {unlockError && (
              <div style={{ color: "var(--danger)", marginBottom: 12, fontSize: 13 }}>
                {unlockError}
              </div>
            )}
            <p style={{ margin: "0 0 16px", fontSize: 11, color: "var(--text-muted)" }}>
              Note: This is a convenience lock for demos and kiosks, not security protection.
            </p>
            <div style={{ display: "flex", gap: 8, justifyContent: "flex-end" }}>
              <button type="button" className="key" onClick={onClose}>
                Cancel
              </button>
              <button type="submit" className="key primary">
                Unlock
              </button>
            </div>
          </form>
        ) : (
          <form onSubmit={handleSave}>
            <p style={{ margin: "0 0 12px", color: "var(--text-muted)" }}>
              Lock controls so end users cannot rearrange the Board or leave the designated pages during demos or presentations.
            </p>

            <div style={{ marginBottom: 14 }}>
              <label style={{ display: "flex", alignItems: "center", gap: 8, cursor: "pointer" }}>
                <input
                  type="checkbox"
                  checked={lockArrange}
                  onChange={(e) => setLockArrange(e.target.checked)}
                />
                <span>Lock Board Arrange button</span>
              </label>
            </div>

            <div style={{ marginBottom: 14 }}>
              <span style={{ display: "block", marginBottom: 6, fontSize: 13, fontWeight: 500 }}>Allowed Pages</span>
              <div style={{ display: "flex", gap: 12 }}>
                {[
                  { id: "station", label: "Station" },
                  { id: "flow", label: "Flow" },
                  { id: "dashboard", label: "Board" },
                ].map((p) => (
                  <label key={p.id} style={{ display: "flex", alignItems: "center", gap: 6, cursor: "pointer" }}>
                    <input
                      type="checkbox"
                      checked={pages.includes(p.id)}
                      onChange={() => togglePage(p.id)}
                    />
                    <span>{p.label}</span>
                  </label>
                ))}
              </div>
            </div>

            <div style={{ marginBottom: 14 }}>
              <span style={{ display: "block", marginBottom: 6, fontSize: 13, fontWeight: 500 }}>Allowed View Modes</span>
              <div style={{ display: "flex", gap: 12 }}>
                {[
                  { id: "navigator", label: "Navigator" },
                  { id: "commander", label: "Commander" },
                  { id: "lookout", label: "Lookout" },
                ].map((m) => (
                  <label key={m.id} style={{ display: "flex", alignItems: "center", gap: 6, cursor: "pointer" }}>
                    <input
                      type="checkbox"
                      checked={modes.includes(m.id)}
                      onChange={() => toggleMode(m.id)}
                    />
                    <span>{m.label}</span>
                  </label>
                ))}
              </div>
            </div>

            <div style={{ marginBottom: 14, paddingTop: 8, borderTop: "1px solid var(--line)" }}>
              <label style={{ display: "block", marginBottom: 6, fontSize: 13, fontWeight: 500 }}>
                {lock?.passcode ? "Change Passcode (leave blank to keep current)" : "Set Unlock Passcode (optional)"}
              </label>
              <input
                type="password"
                className="input-text"
                value={newPasscode}
                onChange={(e) => setNewPasscode(e.target.value)}
                placeholder="Passcode"
                style={{ width: "100%", padding: "6px 8px", marginBottom: 6 }}
              />
              {newPasscode && (
                <input
                  type="password"
                  className="input-text"
                  value={confirmPasscode}
                  onChange={(e) => setConfirmPasscode(e.target.value)}
                  placeholder="Confirm passcode"
                  style={{ width: "100%", padding: "6px 8px" }}
                />
              )}
            </div>

            {configError && (
              <div style={{ color: "var(--danger)", marginBottom: 12, fontSize: 13 }}>
                {configError}
              </div>
            )}

            <p style={{ margin: "0 0 16px", fontSize: 11, color: "var(--text-muted)" }}>
              Note: This is a convenience lock for demos and kiosks, not security protection.
            </p>

            <div style={{ display: "flex", gap: 8, justifyContent: "space-between" }}>
              {lock ? (
                <button type="button" className="key danger" onClick={handleClear}>
                  Remove Lock
                </button>
              ) : <span />}
              <div style={{ display: "flex", gap: 8 }}>
                <button type="button" className="key" onClick={onClose}>
                  Cancel
                </button>
                <button type="submit" className="key primary">
                  Save Lock
                </button>
              </div>
            </div>
          </form>
        )}
      </div>
    </section>
  );
}
