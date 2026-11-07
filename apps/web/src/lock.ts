// UI Lock (#104): lock down parts of the UI (Board Arrange, allowed pages and view modes)
// behind an optional passcode for demos, classrooms, and kiosks.
// Soft convenience lock: salted sha256 hash stored in project file.

import type { Mode, Page } from "./Shell";

export type LockConfig = {
  arrange?: boolean;
  pages?: string[];
  modes?: string[];
  passcode?: {
    algo: string; // "sha256"
    salt: string;
    hash: string;
  };
};

export async function hashPasscode(passcode: string, salt: string): Promise<string> {
  const enc = new TextEncoder();
  const data = enc.encode(salt + ":" + passcode);
  const buf = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(buf))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

export function generateSalt(): string {
  const bytes = new Uint8Array(8);
  crypto.getRandomValues(bytes);
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

export async function verifyPasscode(passcode: string, stored: { salt: string; hash: string }): Promise<boolean> {
  const computed = await hashPasscode(passcode, stored.salt);
  return computed === stored.hash;
}

export function isPageAllowed(page: Page, lock: LockConfig | null | undefined, unlocked: boolean): boolean {
  if (!lock || unlocked) return true;
  if (!lock.pages || lock.pages.length === 0) return true;
  return lock.pages.includes(page);
}

export function isModeAllowed(mode: Mode, lock: LockConfig | null | undefined, unlocked: boolean): boolean {
  if (!lock || unlocked) return true;
  if (!lock.modes || lock.modes.length === 0) return true;
  return lock.modes.includes(mode);
}

export function isArrangeLocked(lock: LockConfig | null | undefined, unlocked: boolean): boolean {
  if (!lock || unlocked) return false;
  return !!lock.arrange;
}
