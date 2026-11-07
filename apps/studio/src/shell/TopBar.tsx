// The top bar (contracts §1): `squero studio [ OM ]` on the left, the menus, and on the right the
// omc chip: `omc 1.27.0 · ready`. Lowercase wordmark, light `studio`, a rounded ink `Om` badge.

import { MenuBar } from "@paperboat/shared/components";
import type { Command } from "@paperboat/shared";
import { chip, type OmcStatus } from "../omc/omc.ts";

export function Wordmark() {
  return (
    <span className="wordmark" aria-label="Squero Studio: OpenModelica">
      <b>squero</b>
      <span className="studio-word">studio</span>
      <span className="om-badge">Om</span>
    </span>
  );
}

export function TopBar({ status, model, commands }: { status: OmcStatus; model?: string; commands: Command[] }) {
  return (
    <header className="bar">
      <Wordmark />
      <MenuBar commands={commands} />
      <span className="spacer" />
      {model && <span className="proj">{model}</span>}
      <span className={`chip omc-${status.state}`} role="status">
        <i aria-hidden="true" />
        {chip(status)}
      </span>
    </header>
  );
}
