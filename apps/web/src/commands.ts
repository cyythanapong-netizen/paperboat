// The command registry (#78): everything the user can do, once. The menu bar, the command
// search and the registry's own shortcuts all read the same list, so a label, a shortcut and
// whether it is enabled can't drift between them. Pure functions and types; the UI is in Menus.tsx.

import { useEffect, useRef } from "react";

export type MenuId = "file" | "edit" | "view" | "run";

/** The menus in the order they appear. A menu with no commands isn't shown. */
export const MENUS: { id: MenuId; label: string }[] = [
  { id: "file", label: "File" },
  { id: "edit", label: "Edit" },
  { id: "view", label: "View" },
  { id: "run", label: "Run" },
];

/** A heading above a group of items, where the group needs one to read. */
export const GROUP_TITLES: Record<string, string> = { pages: "Pages", mode: "Mode", theme: "Theme" };

/** A key combination. `ctrl` also means ⌘ on a Mac. `key` is `KeyboardEvent.key`, lower-case. */
export type Chord = { key: string; ctrl?: boolean; alt?: boolean; shift?: boolean };

export type Command = {
  id: string;
  label: string;
  menu: MenuId;
  /** Commands of one group sit together; the next group is set off by a rule. */
  group: string;
  /** Shown beside the label. Only a `chord` makes the registry handle the key; the rest are
   *  handled where the thing lives (undo, copy and paste on the canvas) and shown here. */
  shortcut?: string;
  chord?: Chord;
  /** A function is read when the menu opens, so it can look at what changed since the last render. */
  enabled?: boolean | (() => boolean);
  /** Why it is unavailable: the item says so (title) instead of hiding. */
  why?: string;
  /** Chosen (a page, a mode, a toggle): drawn in brackets, never filled. */
  checked?: boolean;
  /** Small text at the right, such as a count. */
  meta?: string;
  /** Left out of the command search (the command that opens it). */
  quiet?: boolean;
  run: () => void;
};

export const isEnabled = (c: Command): boolean => (typeof c.enabled === "function" ? c.enabled() : c.enabled ?? true);

/** The key the command search opens with. One place to change it. */
export const SEARCH_CHORD: Chord = { key: "k", ctrl: true };

type KeyLike = Pick<KeyboardEvent, "key" | "ctrlKey" | "metaKey" | "altKey" | "shiftKey">;

export function chordMatches(c: Chord, e: KeyLike): boolean {
  return (
    e.key.toLowerCase() === c.key &&
    (e.ctrlKey || e.metaKey) === !!c.ctrl &&
    e.altKey === !!c.alt &&
    e.shiftKey === !!c.shift
  );
}

/** "Ctrl+Shift+K". */
export function describe(c: Chord): string {
  const key = c.key.length === 1 ? c.key.toUpperCase() : c.key;
  return [c.ctrl && "Ctrl", c.alt && "Alt", c.shift && "Shift", key].filter(Boolean).join("+");
}

/** The commands by menu, in the order given, with the menu titles. */
export function byMenu(commands: Command[]): { id: MenuId; label: string; items: Command[] }[] {
  return MENUS.map((m) => ({ ...m, items: commands.filter((c) => c.menu === m.id) })).filter((m) => m.items.length > 0);
}

/**
 * The commands that match what was typed: every word appears in the label or the menu's name,
 * best first (a label that starts with the word, then one with a word that does, then the rest),
 * in menu order within a rank. Nothing typed: all of them.
 */
export function rank(query: string, commands: Command[]): Command[] {
  const words = query.toLowerCase().split(/\s+/).filter(Boolean);
  const menuName = new Map(MENUS.map((m) => [m.id, m.label.toLowerCase()]));
  const order = new Map(commands.map((c, i) => [c.id, i]));
  const scored: { c: Command; score: number }[] = [];
  for (const c of commands) {
    const label = c.label.toLowerCase();
    const text = `${label} ${menuName.get(c.menu) ?? ""}`;
    if (!words.every((w) => text.includes(w))) continue;
    let score = 0;
    for (const w of words) {
      if (label.startsWith(w)) score += 0;
      else if (label.split(/[\s…]+/).some((p) => p.startsWith(w))) score += 1;
      else if (label.includes(w)) score += 2;
      else score += 3; // only the menu's name matched
    }
    scored.push({ c, score });
  }
  return scored.sort((a, b) => a.score - b.score || (order.get(a.c.id) ?? 0) - (order.get(b.c.id) ?? 0)).map((s) => s.c);
}

/** True while the key is for a field (typing), which keeps its own keys. */
export function typing(target: EventTarget | null): boolean {
  const t = target as HTMLElement | null;
  return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable);
}

/** True inside the menus and the command search, which use the keys for themselves (arrows, Enter, Space). */
export function ownKeys(target: EventTarget | null): boolean {
  return !!(target as HTMLElement | null)?.closest?.("[data-own-keys]");
}

/**
 * The registry's own shortcuts: every command with a `chord` that is enabled. They work even
 * while typing in a field (a chord with Ctrl or Alt isn't text) and are then not a key for the backend.
 */
export function useShortcuts(commands: Command[]) {
  const latest = useRef(commands);
  latest.current = commands;
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.repeat) return;
      const hit = latest.current.find((c) => c.chord && chordMatches(c.chord, e));
      if (!hit) return;
      e.preventDefault(); // not a key for the backend, and not the browser's (Ctrl+K)
      if (isEnabled(hit)) hit.run();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
}
