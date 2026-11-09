// UI Lock (#104) as the app holds it: the project's lock, whether it is unlocked here, and what it
// leaves open (Board Arrange, pages, view modes). A locked view moves to one that is allowed.

import { useEffect, useState } from "react";
import { MODES, PAGES, type Mode, type Page } from "./Shell";
import { isArrangeLocked, type LockConfig } from "./lock";

export const PAGE_IDS = PAGES.map((p) => p.id);

export function useUiLock(
  arrange: boolean,
  setArrange: (on: boolean) => void,
  page: Page,
  setPage: (p: Page) => void,
  mode: Mode,
  setMode: (m: Mode) => void,
) {
  // UI Lock (#104): restrict Board Arrange and available views behind an optional passcode.
  const [projectLock, setProjectLock] = useState<LockConfig | null>(null);
  const [unlocked, setUnlocked] = useState(true);
  const [lockModalOpen, setLockModalOpen] = useState(false);
  const locked = !!projectLock && !unlocked;
  const lockedArrange = isArrangeLocked(projectLock, unlocked);
  const allowedPages = projectLock && !unlocked && projectLock.pages && projectLock.pages.length > 0 ? projectLock.pages : PAGE_IDS;
  const allowedModes: Mode[] =
    projectLock && !unlocked && projectLock.modes && projectLock.modes.length > 0
      ? (projectLock.modes as Mode[])
      : MODES;

  useEffect(() => {
    if (lockedArrange && arrange) setArrange(false);
  }, [lockedArrange, arrange]); // eslint-disable-line react-hooks/exhaustive-deps -- setArrange is a state setter

  useEffect(() => {
    if (locked) {
      if (allowedPages.length > 0 && !allowedPages.includes(page)) {
        setPage(allowedPages[0] as Page);
      }
      if (allowedModes.length > 0 && !allowedModes.includes(mode)) {
        setMode(allowedModes[0] as Mode);
      }
    }
  }, [locked, allowedPages, allowedModes, page, mode, setPage, setMode]);

  return { projectLock, setProjectLock, unlocked, setUnlocked, lockModalOpen, setLockModalOpen, locked, lockedArrange, allowedPages, allowedModes };
}
