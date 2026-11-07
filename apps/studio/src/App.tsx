// Wave 0 scaffold (orchestrator). Lane A owns this file from Wave 3 on: it builds the top bar,
// library, canvas and split view, provides StudioContext, and mounts Lane B's three slots.
import { useState } from "react";
import { ComponentPopover } from "./popover/index.tsx";
import { InterfaceSheet } from "./sheet/index.tsx";
import { StudioDock } from "./dock/index.tsx";

export function App() {
  const [popover, setPopover] = useState<string | null>(null);
  const [sheet, setSheet] = useState(false);
  return (
    <div className="studio" onKeyDown={(e) => e.altKey && e.code === "KeyV" && setSheet((s) => !s)}>
      <header>squero studio [ OM ]</header>
      {popover && <ComponentPopover component={popover} onClose={() => setPopover(null)} />}
      <InterfaceSheet open={sheet} onClose={() => setSheet(false)} />
      <StudioDock />
    </div>
  );
}
