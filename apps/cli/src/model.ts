// The console's pure logic: completion, fuzzy matching, the grey suggestion, history
// search and line editing. No I/O here, so test/*.test.ts runs it directly.

/** A command as `paperboat commands --json` describes it, plus the console's own. */
export interface CommandSpec {
  name: string;
  summary: string;
  aliases: string[];
  subcommands: string[];
  flags: string[];
  /** What the free argument names: backend | toolkit | target | job | ppk | "". */
  arg: string;
}

export interface Candidate {
  value: string;
  detail: string;
  /** Matched character positions in `value`, for highlighting. */
  match: number[];
}

export interface Choice {
  value: string;
  detail: string;
}

/** Where completion values come from: the commands, and values by argument kind. */
export interface Sources {
  commands: CommandSpec[];
  args: Record<string, Choice[]>;
  /** Entries of a folder, for a word that is a path; names only, folders ending in `/`. */
  listDir?: (dir: string) => Choice[];
}

/** A word that names a path rather than a backend: has a `/`, or starts with `.` or `~`. */
export const isPath = (word: string) => /\/|^[.~]/.test(word);

export const FLAG_HELP: Record<string, string> = {
  '--port': 'listen port',
  '--no-build': 'skip the C++ build',
  '--clean': 'clean the build first',
  '--time': 'print build timing',
  '--verbose': 'full build output',
  '--var': 'variable default key=value',
  '--all': 'every one',
  '--json': 'machine-readable output',
  '--json-stream': 'one JSON event per line',
  '--fast': 'manifest and catalog only',
  '--out': 'package path',
  '--dir': 'folder',
  '--host': 'listen host',
  '--daemon': 'run in the background',
  '--version': 'toolkit version',
  '-f': 'follow',
  '-n': 'number of lines',
  '-i': 'interactive',
};

/** Score `query` against `text`: prefix beats substring beats subsequence; null when absent. */
export function fuzzy(query: string, text: string): {score: number; match: number[]} | null {
  if (!query) return {score: 0, match: []};
  const t = text.toLowerCase();
  const q = query.toLowerCase();
  const at = t.indexOf(q);
  if (at >= 0) {
    return {score: at === 0 ? 100 - text.length / 100 : 50, match: [...q].map((_, k) => at + k)};
  }
  const match: number[] = [];
  for (let i = 0, j = 0; i < t.length && j < q.length; i++) {
    if (t[i] === q[j]) {
      match.push(i);
      j++;
    }
  }
  return match.length === q.length ? {score: 10 - match[match.length - 1] / 100, match} : null;
}

/** Split a command line into words; quotes group, as in the C++ prompt. */
export function splitWords(line: string): string[] {
  const words: string[] = [];
  let cur = '';
  let quote = '';
  let has = false;
  for (const c of line) {
    if (quote) {
      if (c === quote) quote = '';
      else cur += c;
    } else if (c === '"' || c === "'") {
      quote = c;
      has = true;
    } else if (/\s/.test(c)) {
      if (cur || has) words.push(cur);
      cur = '';
      has = false;
    } else {
      cur += c;
    }
  }
  if (cur || has) words.push(cur);
  return words;
}

export function findCommand(commands: CommandSpec[], name: string): CommandSpec | undefined {
  return commands.find(c => c.name === name || c.aliases.includes(name));
}

/** Completions for the word under the cursor (the line's end). */
export function complete(line: string, sources: Sources): {candidates: Candidate[]; from: number; word: string} {
  const parts = line.split(' ');
  const word = parts[parts.length - 1];
  const from = line.length - word.length;
  const index = parts.length - 1;
  let pool: Choice[] = [];
  if (index === 0) {
    pool = sources.commands.map(c => ({value: c.name, detail: c.summary}));
    if (word) {
      for (const c of sources.commands) for (const a of c.aliases) pool.push({value: a, detail: `same as ${c.name}`});
    }
  } else {
    const cmd = findCommand(sources.commands, parts[0]);
    if (!cmd) return {candidates: [], from, word};
    if (word.startsWith('-')) pool = cmd.flags.map(f => ({value: f, detail: FLAG_HELP[f] ?? ''}));
    else if (cmd.subcommands.length && index === 1) pool = cmd.subcommands.map(s => ({value: s, detail: ''}));
    else if ((cmd.arg === 'backend' || cmd.arg === 'ppk') && isPath(word) && sources.listDir) {
      // Complete the last segment; the candidate is the whole path so far.
      const cut = word.lastIndexOf('/') + 1;
      const dir = word.slice(0, cut);
      const rest = word.slice(cut);
      const pool2 = sources.listDir(dir || '.');
      return {
        candidates: pool2
          .flatMap(p => {
            const m = fuzzy(rest, p.value);
            return m && (!rest || p.value.toLowerCase().startsWith(rest.toLowerCase()))
              ? [{value: dir + p.value, detail: p.detail, match: m.match.map(i => i + dir.length)}]
              : [];
          })
          .sort((a, b) => a.value.localeCompare(b.value)),
        from,
        word,
      };
    } else if (cmd.arg) pool = sources.args[cmd.arg] ?? [];
  }
  const candidates = pool
    .map(p => ({p, m: fuzzy(word, p.value)}))
    .flatMap(({p, m}) => (m ? [{...p, score: m.score, match: m.match}] : []))
    .sort((a, b) => b.score - a.score)
    .map(({value, detail, match}) => ({value, detail, match}));
  return {candidates, from, word};
}

/** The grey suggestion after the cursor: the newest history line that extends the input,
 * else the top completion when it extends the current word. */
export function ghost(input: string, history: string[], sources: Sources): string {
  if (!input) return '';
  for (let i = history.length - 1; i >= 0; i--) {
    const h = history[i];
    if (h.startsWith(input) && h.length > input.length) return h.slice(input.length);
  }
  const {candidates, word} = complete(input, sources);
  const top = candidates[0];
  return word && top && top.value.startsWith(word) ? top.value.slice(word.length) : '';
}

/** History lines containing `query`, newest first, without repeats. */
export function searchHistory(history: string[], query: string): string[] {
  return [...new Set([...history].reverse())].filter(h => h.includes(query));
}

/** Append a line to history: skip blanks and an immediate repeat, keep the newest `max`. */
export function remember(history: string[], line: string, max = 1000): string[] {
  const l = line.trim();
  if (!l || history[history.length - 1] === l) return history;
  return [...history, l].slice(-max);
}

/** Keep the newest `max` lines of a log. */
export function pushLine(log: string[], line: string, max = 2000): void {
  log.push(line);
  if (log.length > max) log.splice(0, log.length - max);
}

export interface Line {
  text: string;
  cursor: number;
}

export interface EditKey {
  input: string;
  ctrl?: boolean;
  backspace?: boolean;
  delete?: boolean;
  left?: boolean;
  right?: boolean;
  home?: boolean;
  end?: boolean;
}

/** One editing key on the input line; null when the key isn't an edit. */
export function edit(line: Line, key: EditKey): Line | null {
  const {text, cursor} = line;
  if (key.ctrl) {
    switch (key.input) {
      case 'a':
        return {text, cursor: 0};
      case 'e':
        return {text, cursor: text.length};
      case 'u':
        return {text: text.slice(cursor), cursor: 0};
      case 'k':
        return {text: text.slice(0, cursor), cursor};
      case 'w': {
        const start = text.slice(0, cursor).replace(/\S+\s*$/, '').length;
        return {text: text.slice(0, start) + text.slice(cursor), cursor: start};
      }
      default:
        return null;
    }
  }
  // Terminals send DEL for Backspace; Ink reports it as delete.
  if (key.backspace || key.delete) {
    return cursor ? {text: text.slice(0, cursor - 1) + text.slice(cursor), cursor: cursor - 1} : line;
  }
  if (key.left) return {text, cursor: Math.max(0, cursor - 1)};
  if (key.right) return {text, cursor: Math.min(text.length, cursor + 1)};
  if (key.home) return {text, cursor: 0};
  if (key.end) return {text, cursor: text.length};
  // Typed or pasted text; a paste's newlines and tabs become spaces, other controls go.
  const typed = key.input.replace(/[\t\r\n]+/g, ' ').replace(/[\x00-\x1f\x7f]/g, '');
  if (typed) return {text: text.slice(0, cursor) + typed + text.slice(cursor), cursor: cursor + typed.length};
  return null;
}

/** A job's name for a target: a known backend's name, else the folder or package it names
 * (`../x/cart-pole-cpp/` → `cart-pole`, `parts-0.1.0.ppk` → `parts-0.1.0`). */
export function targetName(target: string): string {
  const base = target.replace(/\/+$/, '').split('/').pop() || target;
  if (base === '.' || base === '') return 'backend';
  return base.replace(/\.ppk$/, '').replace(/-(cpp|python)$/, '');
}
