import assert from "node:assert/strict";
import { test } from "node:test";
import { byMenu, chordMatches, describe, isEnabled, rank, type Command } from "../src/commands.ts";

const cmd = (id: string, label: string, menu: Command["menu"], extra: Partial<Command> = {}): Command => ({
  id,
  label,
  menu,
  group: "g",
  run: () => {},
  ...extra,
});

const all = [
  cmd("file.backend", "Backend…", "file"),
  cmd("edit.undo", "Undo", "edit"),
  cmd("edit.addBlock", "Add block…", "edit"),
  cmd("edit.addObject", "Add station object…", "edit"),
  cmd("view.console", "Console", "view"),
  cmd("run.run", "Run", "run"),
  cmd("run.cancel", "Cancel", "run"),
];

const key = (k: string, mods: { ctrl?: boolean; alt?: boolean; shift?: boolean; meta?: boolean } = {}) => ({
  key: k,
  ctrlKey: !!mods.ctrl,
  metaKey: !!mods.meta,
  altKey: !!mods.alt,
  shiftKey: !!mods.shift,
});

test("a chord matches its key and exactly its modifiers", () => {
  const k = { key: "k", ctrl: true };
  assert.ok(chordMatches(k, key("k", { ctrl: true })));
  assert.ok(chordMatches(k, key("K", { ctrl: true })), "case does not matter");
  assert.ok(chordMatches(k, key("k", { meta: true })), "⌘ counts as Ctrl");
  assert.ok(!chordMatches(k, key("k")), "no modifier");
  assert.ok(!chordMatches(k, key("k", { ctrl: true, shift: true })), "extra modifier");
  assert.ok(!chordMatches(k, key("j", { ctrl: true })));
  assert.ok(chordMatches({ key: "1", alt: true }, key("1", { alt: true })));
  assert.ok(!chordMatches({ key: "1", alt: true }, key("1", { alt: true, ctrl: true })));
});

test("a chord is described the way it is typed", () => {
  assert.equal(describe({ key: "k", ctrl: true }), "Ctrl+K");
  assert.equal(describe({ key: "z", ctrl: true, shift: true }), "Ctrl+Shift+Z");
  assert.equal(describe({ key: "1", alt: true }), "Alt+1");
});

test("commands are grouped by menu in menu order, empty menus left out", () => {
  const menus = byMenu(all);
  assert.deepEqual(menus.map((m) => m.id), ["file", "edit", "view", "run"]);
  assert.deepEqual(byMenu([cmd("a", "A", "run")]).map((m) => m.id), ["run"]);
});

test("search: nothing typed lists every command in order", () => {
  assert.deepEqual(rank("", all).map((c) => c.id), all.map((c) => c.id));
  assert.deepEqual(rank("   ", all).map((c) => c.id), all.map((c) => c.id));
});

test("search: every word must match; a label that starts with it comes first", () => {
  assert.deepEqual(rank("add", all).map((c) => c.id), ["edit.addBlock", "edit.addObject"]);
  assert.deepEqual(rank("add obj", all).map((c) => c.id), ["edit.addObject"]);
  // "run" starts the Run label, and is only the menu's name for Cancel.
  assert.deepEqual(rank("run", all).map((c) => c.id), ["run.run", "run.cancel"]);
  // a word inside a label ranks under one that starts it.
  assert.equal(rank("o", all)[0].id, "edit.addObject", "'object' starts with o; 'block' and 'console' only contain it");
  assert.deepEqual(rank("zzz", all), []);
});

test("search: the menu's name finds its commands", () => {
  assert.deepEqual(rank("edit", all).map((c) => c.id), ["edit.undo", "edit.addBlock", "edit.addObject"]);
});

test("enabled: absent is on; a function is read each time", () => {
  assert.ok(isEnabled(cmd("a", "A", "run")));
  assert.ok(!isEnabled(cmd("a", "A", "run", { enabled: false })));
  let on = false;
  const c = cmd("a", "A", "run", { enabled: () => on });
  assert.ok(!isEnabled(c));
  on = true;
  assert.ok(isEnabled(c));
});
