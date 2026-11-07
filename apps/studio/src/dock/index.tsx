import { useEffect, useState } from "react";
import { useStudio, type ExportProgress } from "../model/types.ts";
import {
  checkModelBalance,
  defaultExportFolder,
  findDanglingFlowPorts,
  fixDanglingPort,
  runBackendExport,
  type BalanceResult,
  type DanglingPort,
  type ExportOutcome,
} from "./dock.ts";
import "./dock.css";

export function StudioDock() {
  const api = useStudio();
  const [balance, setBalance] = useState<BalanceResult>({
    kind: "balanced",
    equations: 0,
    unknowns: 0,
    ratio: "… / …",
    words: "checking balance…",
  });
  const [outDir, setOutDir] = useState<string>(() => defaultExportFolder(api.model.name));
  const [progress, setProgress] = useState<ExportProgress | null>(null);
  const [outcome, setOutcome] = useState<ExportOutcome | null>(null);
  const [fixError, setFixError] = useState<string | null>(null);
  const [revision, setRevision] = useState(0);

  const modelSignature = JSON.stringify({
    name: api.model.name,
    components: api.model.components,
    connections: api.model.connections,
    params: api.model.params,
    entries: api.model.entries,
    revision,
  });

  useEffect(() => {
    setOutDir((prev) => (prev ? prev : defaultExportFolder(api.model.name)));
  }, [api.model.name]);

  // Keyed on the model and omc only: api itself is rebuilt on every status push, and
  // checkModel causes one, so depending on api re-checks forever.
  const omc = api.omc;
  const modelName = api.model.name;
  useEffect(() => {
    let cancelled = false;
    const timer = setTimeout(() => {
      void checkModelBalance({ omc, model: { name: modelName } }).then((res) => {
        if (!cancelled) setBalance(res);
      });
    }, 300);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [omc, modelName, modelSignature]);

  const dangling = findDanglingFlowPorts(api.model);
  const isBalanced = balance.kind === "balanced";

  const handleFix = async (item: DanglingPort) => {
    setFixError(null);
    const err = await fixDanglingPort(api, item);
    if (err) setFixError(err);
    setRevision((r) => r + 1);
  };

  const handleExport = async () => {
    if (!isBalanced) return;
    setOutcome(null);
    setProgress({ stage: "check", fraction: 0 });
    const res = await runBackendExport(api, outDir, (p) => {
      setProgress({ ...p });
    });
    setOutcome(res);
  };

  return (
    <div className="studio-dock" role="region" aria-label="Model balance and export" data-own-keys>
      {progress && (
        <div className="studio-dock-progress-track" aria-hidden="true">
          <div
            className="studio-dock-progress-bar"
            style={{ width: `${Math.max(0, Math.min(100, Math.round(progress.fraction * 100)))}%` }}
          />
        </div>
      )}

      <div className="studio-dock-row">
        <output
          className={`studio-dock-balance${isBalanced ? "" : " unbalanced"}`}
          aria-label="Equation and unknown balance"
        >
          <span className="studio-dock-dot" aria-hidden="true" />
          <span>{balance.words}</span>
        </output>

        {dangling.length > 0 && (
          <div className="studio-dock-dangling" aria-label="Dangling flow connectors">
            {dangling.map((item) => (
              <span key={item.portPath} className="studio-dock-dangling-item">
                <span>{item.label}</span>
                {item.fix && (
                  <button
                    type="button"
                    className="studio-dock-fix-key"
                    onClick={() => void handleFix(item)}
                  >
                    [ {item.fix.buttonLabel} ]
                  </button>
                )}
              </span>
            ))}
          </div>
        )}

        <div className="studio-dock-export">
          <label className="studio-dock-out-well">
            <input
              className="studio-dock-out-input"
              aria-label="Export backend folder"
              spellCheck={false}
              value={outDir}
              placeholder={defaultExportFolder(api.model.name)}
              onChange={(e) => setOutDir(e.target.value)}
            />
          </label>
          <button
            type="button"
            className="studio-dock-export-key"
            disabled={!isBalanced}
            title={isBalanced ? `Export C++ backend to ${outDir}` : balance.words}
            onClick={() => void handleExport()}
          >
            Export backend
          </button>
        </div>
      </div>

      {(progress || outcome || fixError) && (
        <div className="studio-dock-row">
          {progress && <span className="studio-dock-stage">{progress.stage}</span>}
          {outcome && (
            <span className={`studio-dock-result${outcome.ok ? "" : " error"}`}>
              {outcome.line}
            </span>
          )}
          {fixError && <span className="studio-dock-result error">{fixError}</span>}
        </div>
      )}
    </div>
  );
}
