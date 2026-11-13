// Toolkits (#118): asking for the ones a project needs and this backend lacks, and telling the
// user how to install them from the repository or folder they trust. Installation is never
// triggered from the manifest: it is an explicit CLI action.

import { useCallback, useEffect, useRef, useState } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, ToolkitStatus } from "./protocol";
import { missingRequiredToolkits } from "./Toolkits";
import { isBlock, type FlowNode } from "./flowModel";

export function useToolkitInstall(
  state: EngineState,
  send: (m: ClientMsg) => void,
  log: (level: "info" | "warn" | "error", text: string) => void,
  nodes: FlowNode[],
) {
  const promptedToolkits = useRef<string>("");
  const prevBusyToolkits = useRef<Map<string, string>>(new Map());
  const [missingDialog, setMissingDialog] = useState<ToolkitStatus[] | null>(null);

  // What we asked for that the backend has not acknowledged yet, so the keys dim and say "installing…"
  // on the press instead of a round trip later. One toolkit operation at a time: the backend runs
  // them one after another, so a second click would only queue a request that fails (#341).
  const [requested, setRequested] = useState<Record<string, "installing" | "uninstalling">>({});
  const requestedRef = useRef(requested);
  const request = (name: string, action: "installing" | "uninstalling") => {
    if (Object.keys(requestedRef.current).length > 0) return false;
    if (state.toolkits.some((t) => t.busy)) return false;
    requestedRef.current = { [name]: action };
    setRequested(requestedRef.current);
    return true;
  };
  // Its `busy` word takes over as soon as it lands; a lost connection or a backend that never
  // answers (a few seconds is well past a round trip) gives the keys back.
  useEffect(() => {
    if (Object.keys(requested).length === 0) return;
    if (state.connection !== "open" || state.toolkits.some((t) => t.busy)) {
      requestedRef.current = {};
      setRequested({});
      return;
    }
    const timer = setTimeout(() => {
      requestedRef.current = {};
      setRequested({});
    }, 10_000);
    return () => clearTimeout(timer);
  }, [requested, state.connection, state.toolkits]);

  const restartBackend = useCallback(() => {
    log("info", "restarting backend…");
    send({ type: "backend.restart" });
  }, [log, send]);
  const installToolkit = useCallback(
    (name: string, version?: string) => {
      if (!request(name, "installing")) return;
      log("info", `requesting install of toolkit "${name}"${version ? ` (${version})` : ""}…`);
      send({ type: "toolkit.install", name, version });
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps -- request() reads the latest state through the closure
    [log, send, state.toolkits],
  );
  const toolkitVersions = useCallback(
    (name: string) => {
      send({ type: "toolkit.versions", name });
    },
    [send],
  );
  const uninstallToolkit = useCallback(
    (name: string) => {
      if (!request(name, "uninstalling")) return;
      log("info", `requesting uninstall of toolkit "${name}"…`);
      send({ type: "toolkit.uninstall", name });
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps -- request() reads the latest state through the closure
    [log, send, state.toolkits],
  );
  useEffect(() => {
    if (state.connection !== "open") return;
    const missingTypes = nodes.filter(isBlock).filter((n) => n.data.missing).map((n) => n.data.def.type);
    const missing = missingRequiredToolkits(state.toolkits, missingTypes);
    if (missing.length === 0) {
      if (missingDialog) setMissingDialog(null);
      return;
    }
    const key = `${state.engine}:${missing.map((t) => t.name).sort().join(",")}`;
    if (promptedToolkits.current === key) return;
    promptedToolkits.current = key;
    setMissingDialog(missing);
  }, [state.connection, state.engine, state.toolkits, nodes, missingDialog]);

  useEffect(() => {
    const prev = prevBusyToolkits.current;
    const finished: string[] = [];
    const failed: string[] = [];
    for (const t of state.toolkits) {
      const was = prev.get(t.name);
      if ((was === "installing" || was === "updating") && !t.busy) {
        if (t.installed) {
          finished.push(t.name);
        } else {
          failed.push(t.name);
        }
      }
    }
    const next = new Map<string, string>();
    for (const t of state.toolkits) if (t.busy) next.set(t.name, t.busy);
    prevBusyToolkits.current = next;

    if (failed.length > 0) {
      log("error", `Installing ${failed.join(", ")} failed. See the lines above for details.`);
      return;
    }

    if (finished.length > 0) {
      const names = finished.join(", ");
      log("warn", `Toolkit ${names} ready: restart the backend so it loads the update.`);
    }
  }, [state.toolkits, log]);

  return { missingDialog, setMissingDialog, restartBackend, installToolkit, toolkitVersions, uninstallToolkit, requestedToolkits: requested };
}
