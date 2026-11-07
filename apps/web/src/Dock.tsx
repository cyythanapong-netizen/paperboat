import type { ReactNode } from "react";
import type { RunSettings } from "@paperboat/shared";
import { Dock as SharedDock, BackendWindow } from "@paperboat/shared/components";
import { RunOptions } from "./RunOptions";
import { VarControls } from "./VarControls";

export { BackendWindow };

export function Dock({
  canRun,
  whyNot,
  running,
  status,
  statusText,
  defaults,
  settings,
  onSettings,
  onRun,
  onCancel,
}: {
  canRun: boolean;
  whyNot?: string;
  running: boolean;
  status?: string;
  statusText?: ReactNode;
  defaults: RunSettings;
  settings: RunSettings;
  onSettings: (v: RunSettings) => void;
  onRun: () => void;
  onCancel: () => void;
}) {
  return (
    <SharedDock
      canRun={canRun}
      whyNot={whyNot}
      running={running}
      status={status}
      statusText={statusText}
      optionsSlot={<RunOptions defaults={defaults} value={settings} onChange={onSettings} />}
      controlsSlot={<VarControls flag="dock" />}
      onRun={onRun}
      onCancel={onCancel}
    />
  );
}
