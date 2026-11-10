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

  const restartBackend = useCallback(() => {
    log("info", "restarting backend…");
    send({ type: "backend.restart" });
  }, [log, send]);
  const installToolkit = useCallback(
    (name: string, version?: string) => {
      log("info", `requesting install of toolkit "${name}"${version ? ` (${version})` : ""}…`);
      send({ type: "toolkit.install", name, version });
    },
    [log, send],
  );
  const toolkitVersions = useCallback(
    (name: string) => {
      send({ type: "toolkit.versions", name });
    },
    [send],
  );
  const uninstallToolkit = useCallback(
    (name: string) => {
      log("info", `requesting uninstall of toolkit "${name}"…`);
      send({ type: "toolkit.uninstall", name });
    },
    [log, send],
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

  return { missingDialog, setMissingDialog, restartBackend, installToolkit, toolkitVersions, uninstallToolkit };
}
