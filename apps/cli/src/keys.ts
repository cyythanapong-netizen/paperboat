// Keys, by what has the screen: a quit question, a picker, the dashboard, the toolkit
// manager, a followed log, history search, or the prompt.

import type {Key} from 'ink';

import {complete, edit, ghost, searchHistory} from './model.ts';
import {
  S, changed, clearScreen, dashRows, exec, exitNow, mu, openLogs, pickList, requestExit, restartJobById,
  restartTarget, say, sources, stopJobById, stopTarget, toolkitOps,
} from './store.ts';

const clamp = (i: number, n: number) => Math.max(0, Math.min(i, n - 1));

export function handleKey(input: string, key: Key): void {
  const ctrlC = key.ctrl && input === 'c';
  if (S.confirm) {
    if (input === 'y' || key.return) void exitNow();
    else if (input === 'n' || key.escape || ctrlC) {
      S.confirm = false;
      say([mu('still here')]);
    }
    return changed();
  }
  switch (S.mode) {
    case 'picker':
      return picker(input, key, ctrlC);
    case 'dash':
      return dash(input, key, ctrlC);
    case 'toolkit':
      return toolkits(input, key, ctrlC);
    case 'logs':
      if (key.escape || input === 'q' || ctrlC) S.mode = S.logs.back;
      return changed();
  }
  if (S.search) return search(input, key, ctrlC);
  prompt(input, key, ctrlC);
  changed();
}

function picker(input: string, key: Key, ctrlC: boolean): void {
  const list = pickList(S.picker.query);
  if (key.downArrow) S.picker.i = clamp(S.picker.i + 1, list.length);
  else if (key.upArrow) S.picker.i = clamp(S.picker.i - 1, list.length);
  else if (key.escape || ctrlC) {
    S.mode = 'prompt';
    say([mu('run cancelled')]);
  } else if (key.return) {
    const pick = list[S.picker.i];
    S.mode = 'prompt';
    if (pick) exec(`run ${pick.backend.name}`);
  } else if (key.backspace || key.delete) {
    S.picker.query = S.picker.query.slice(0, -1);
    S.picker.i = 0;
  } else if (input && !key.ctrl && !key.meta) {
    S.picker.query += input;
    S.picker.i = 0;
  }
  changed();
}

function dash(input: string, key: Key, ctrlC: boolean): void {
  const rows = dashRows();
  const row = rows[S.dash.i];
  S.hint = '';
  if (key.downArrow) S.dash.i = clamp(S.dash.i + 1, rows.length);
  else if (key.upArrow) S.dash.i = clamp(S.dash.i - 1, rows.length);
  else if (key.escape || input === 'q' || ctrlC) S.mode = 'prompt';
  else if (row && input === 'r') {
    if (row.job) restartJobById(row.job);
    else restartTarget(String(row.ext!.port));
  } else if (row && input === 's') {
    if (row.job) stopJobById(row.job);
    else stopTarget(String(row.ext!.port));
  } else if (row && key.return) {
    if (row.job) openLogs(row.job, 'dash');
    else S.hint = `${row.ext!.name} was started outside this console · no log here`;
  }
  changed();
}

function toolkits(input: string, key: Key, ctrlC: boolean): void {
  const n = S.toolkits.length;
  const cur = S.toolkits[S.tk.i];
  if (key.downArrow) S.tk.i = clamp(S.tk.i + 1, n);
  else if (key.upArrow) S.tk.i = clamp(S.tk.i - 1, n);
  else if (input === ' ' && n && !cur?.builtin) S.tk.want[S.tk.i] = !S.tk.want[S.tk.i];
  else if (input === 'u' && cur?.installed && !cur?.builtin) {
    S.mode = 'prompt';
    void toolkitOps([{op: 'update', name: cur.name}]);
  } else if (key.escape || ctrlC) S.mode = 'prompt';
  else if (key.return) {
    const ops = S.toolkits.flatMap((t, i) =>
      t.builtin || S.tk.want[i] === t.installed ? [] : [{op: S.tk.want[i] ? 'install' : 'uninstall', name: t.name}],
    );
    S.mode = 'prompt';
    if (ops.length) void toolkitOps(ops);
    else say([mu('toolkits unchanged')]);
  }
  changed();
}

function search(input: string, key: Key, ctrlC: boolean): void {
  const s = S.search!;
  const matches = searchHistory(S.history, s.query);
  if (key.ctrl && input === 'r') s.i = clamp(s.i + 1, matches.length);
  else if (key.escape || ctrlC) S.search = null;
  else if (key.return || key.tab || key.rightArrow) {
    const m = matches[s.i];
    if (m) S.line = {text: m, cursor: m.length};
    S.search = null;
  } else if (key.backspace || key.delete) {
    s.query = s.query.slice(0, -1);
    s.i = 0;
  } else if (input && !key.ctrl && !key.meta) {
    s.query += input;
    s.i = 0;
  }
  changed();
}

function tab(dir: number): void {
  if (S.menu) {
    const n = S.menu.candidates.length;
    S.menu.i = (S.menu.i + dir + n) % n;
    const text = S.line.text.slice(0, S.menu.from) + S.menu.candidates[S.menu.i].value;
    S.line = {text, cursor: text.length};
    return;
  }
  const before = S.line.text.slice(0, S.line.cursor);
  const all = complete(before, sources());
  const from = all.from;
  // Like a shell: prefix matches win; fuzzy ones only when nothing starts with the word.
  const prefixed = all.candidates.filter(c => c.value.startsWith(all.word));
  const candidates = prefixed.length ? prefixed : all.candidates;
  if (!candidates.length) return;
  const one = candidates.length === 1;
  // A single match ends the word, unless it's a folder: then you go on typing inside it.
  const text = before.slice(0, from) + candidates[0].value + (one && !candidates[0].value.endsWith('/') ? ' ' : '');
  S.line = {text: text + S.line.text.slice(S.line.cursor), cursor: text.length};
  if (!one) S.menu = {candidates, from, i: 0};
}

function prompt(input: string, key: Key, ctrlC: boolean): void {
  if (ctrlC) {
    S.menu = null;
    if (S.line.text) {
      S.line = {text: '', cursor: 0};
      S.ctrlc = false;
      S.hint = '';
    } else if (S.ctrlc) {
      S.ctrlc = false;
      S.hint = '';
      requestExit();
    } else {
      S.ctrlc = true;
      S.hint = 'press ctrl+c again to quit';
    }
    return;
  }
  S.ctrlc = false;
  S.hint = '';
  if (key.ctrl && input === 'r') {
    S.menu = null;
    S.search = {query: '', i: 0};
    return;
  }
  if (key.ctrl && input === 'l') return clearScreen();
  if (key.ctrl && input === 'd' && !S.line.text) return requestExit();
  if (key.tab) return tab(key.shift ? -1 : 1);
  if (S.menu && (key.downArrow || key.upArrow)) return tab(key.downArrow ? 1 : -1);
  if (S.menu && key.escape) {
    S.menu = null;
    return;
  }
  if (key.return) {
    if (S.menu) {
      S.menu = null;
      S.line = {text: `${S.line.text} `, cursor: S.line.text.length + 1};
      return;
    }
    const text = S.line.text;
    S.line = {text: '', cursor: 0};
    return exec(text);
  }
  S.menu = null;
  if (key.upArrow || key.downArrow) {
    const h = S.history;
    if (key.upArrow && h.length) S.hi = S.hi < 0 ? h.length - 1 : Math.max(0, S.hi - 1);
    else if (key.downArrow && S.hi >= 0) S.hi = S.hi + 1 < h.length ? S.hi + 1 : -1;
    else return;
    const text = S.hi < 0 ? '' : h[S.hi];
    S.line = {text, cursor: text.length};
    return;
  }
  // → or End at the end of the line takes the grey suggestion.
  if ((key.rightArrow || key.end) && S.line.cursor === S.line.text.length) {
    const g = ghost(S.line.text, S.history, sources());
    if (g) {
      S.line = {text: S.line.text + g, cursor: S.line.text.length + g.length};
      return;
    }
  }
  const next = edit(S.line, {
    input, ctrl: key.ctrl, backspace: key.backspace, delete: key.delete,
    left: key.leftArrow, right: key.rightArrow, home: key.home, end: key.end,
  });
  if (next) {
    S.line = next;
    S.hi = -1;
  }
}

