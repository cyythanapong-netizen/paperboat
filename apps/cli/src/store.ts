// The console's state and everything that changes it: jobs (long-running `paperboat run` /
// `app start` / `build`), tasks (check, pack, toolkit changes, one-shot commands) and the
// commands typed at the prompt. Components read `S` and re-render on `changed()`.

import {execFile} from 'node:child_process';
import {appendFileSync, mkdirSync, readFileSync, readdirSync} from 'node:fs';
import {homedir} from 'node:os';
import {join} from 'node:path';
import type {ChildProcess} from 'node:child_process';
import {useSyncExternalStore} from 'react';

import {
  type Candidate,
  type Choice,
  type CommandSpec,
  type Line,
  type Sources,
  findCommand,
  fuzzy,
  isInstance,
  nextInstanceName,
  parseInstanceName,
  pushLine,
  remember,
  splitWords,
  targetName,
} from './model.ts';
import * as pb from './paperboat.ts';

/* ---------- transcript lines ---------- */

export type Tone = 'muted' | 'faint' | 'signal' | 'hi' | 'danger' | 'bold';
export interface Seg {
  t: string;
  tone?: Tone;
}
export type Out = Seg[];

const s = (t: string, tone?: Tone): Seg => ({t, tone});
export const mu = (t: string) => s(t, 'muted');
export const fa = (t: string) => s(t, 'faint');
export const sg = (t: string) => s(t, 'signal');
export const dg = (t: string) => s(t, 'danger');
export const b = (t: string) => s(t, 'bold');
export const sec = (ms: number) => `${(ms / 1000).toFixed(1)}s`;

/* ---------- state ---------- */

export type Mode = 'prompt' | 'picker' | 'toolkit' | 'dash' | 'logs';

export interface Job {
  id: number;
  name: string;
  kind: 'backend' | 'web' | 'build';
  args: string[];
  port: number;
  pid: number;
  state: 'building' | 'running' | 'exited';
  code: number | null;
  stopped: boolean;
  t0: number;
  ready: number;
  /** A backend already on the port when this one started: not this job's sign of life. */
  stale: number;
  log: string[];
  child: ChildProcess | null;
}

export interface Step {
  name: string;
  state: 'waiting' | 'running' | 'done' | 'failed' | 'skipped';
  message: string;
  t0: number;
  duration: number;
}

export interface Op {
  op: string;
  name: string;
  state: 'queued' | 'running' | 'done' | 'failed';
  t0: number;
  log: string[];
}

export interface Task {
  id: number;
  kind: 'check' | 'pack' | 'unpack' | 'toolkit' | 'plain';
  title: string;
  t0: number;
  steps: Step[];
  progress: {files: number; total: number; bytes: number};
  ops: Op[];
}

export interface External {
  name: string;
  port: number;
  pid: number;
  /** A backend, or an app (the web app, squero-studio-om). */
  kind: 'backend' | 'app';
}

/** An app from apps.toml (`pkg list --json`): a program can be started, a library only builds. */
export interface Program {
  name: string;
  kind: 'program' | 'library';
  launch: string;
  port: number;
  installed: boolean;
  description: string;
}

export interface Backend {
  name: string;
  language: string;
  port: number;
  dir: string;
  description: string;
  source: string;
}

export interface Toolkit {
  name: string;
  installed: boolean;
  builtin?: boolean;
  version?: string;
  description: string;
}

// The console's own commands, next to the C++ table from `paperboat commands --json`.
const OWN: CommandSpec[] = [
  {name: 'logs', summary: 'Show a job log (-f follows it)', aliases: [], subcommands: [], flags: ['-f', '-n'], arg: 'job'},
  {name: 'clear', summary: 'Clear the screen', aliases: [], subcommands: [], flags: [], arg: ''},
  {name: 'help', summary: 'List commands and keys', aliases: [], subcommands: [], flags: [], arg: ''},
  {name: 'exit', summary: 'Leave the console', aliases: ['quit'], subcommands: [], flags: [], arg: ''},
];

export const S = {
  mode: 'prompt' as Mode,
  line: {text: '', cursor: 0} as Line,
  menu: null as null | {candidates: Candidate[]; from: number; i: number},
  history: [] as string[],
  hi: -1,
  search: null as null | {query: string; i: number},
  transcript: [] as Out[],
  clears: 0,
  jobs: [] as Job[],
  tasks: [] as Task[],
  external: [] as External[],
  backends: [] as Backend[],
  programs: [] as Program[],
  toolkits: [] as Toolkit[],
  commands: OWN as CommandSpec[],
  picker: {query: '', i: 0},
  dash: {i: 0},
  tk: {i: 0, want: [] as boolean[]},
  logs: {job: 0, back: 'prompt' as Mode},
  confirm: false,
  ctrlc: false,
  hint: '',
  git: '',
  version: '',
  quit: false,
};

let version = 0;
const listeners = new Set<() => void>();
export function changed(): void {
  version++;
  for (const l of listeners) l();
}
export function useStore(): number {
  return useSyncExternalStore(
    l => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    () => version,
  );
}

export function say(line: Out): void {
  S.transcript = [...S.transcript, line];  // a new array: <Static> notices only a new reference
  changed();
}

export const alive = (j: Job) => j.state !== 'exited';
export const busy = () => S.jobs.some(j => j.state === 'building') || S.tasks.length > 0 || S.mode === 'dash';

/** The newest job by that name (or instance base name / port): a live service first, then any live one, then any. */
export function jobBy(name: string): Job | undefined {
  const exact = S.jobs.filter(j => j.name === name || (name !== '' && String(j.port) === name));
  if (exact.length) {
    return exact.filter(j => alive(j) && j.kind !== 'build').pop() ?? exact.filter(alive).pop() ?? exact[exact.length - 1];
  }
  const byBase = S.jobs.filter(j => isInstance(j.name, name));
  return (
    byBase.filter(j => alive(j) && j.kind !== 'build').pop() ?? byBase.filter(alive).pop() ?? byBase[byBase.length - 1]
  );
}

/** All live services (not builds) matching an exact instance name, port, or base backend name. */
export function servicesBy(target: string): Job[] {
  const exact = S.jobs.filter(j => alive(j) && j.kind !== 'build' && (j.name === target || String(j.port) === target));
  if (exact.length) return exact;
  return S.jobs.filter(j => alive(j) && j.kind !== 'build' && isInstance(j.name, target));
}

/** A live service (not a build) by name, base name, or port. */
export const serviceBy = (target: string): Job | undefined => {
  const matches = servicesBy(target);
  return matches[matches.length - 1];
};

export function sources(): Sources {
  let ppk: string[] = [];
  try {
    ppk = readdirSync('.').filter(f => f.endsWith('.ppk'));
  } catch {
    // unreadable folder: no packages to offer
  }
  const liveServices = S.jobs.filter(j => alive(j) && j.kind !== 'build');
  const baseTargets = [...new Set(liveServices.map(j => parseInstanceName(j.name).base))].filter(
    base => !liveServices.some(j => j.name === base),
  );
  return {
    commands: S.commands,
    listDir,
    args: {
      backend: [
        ...S.backends.map(x => ({value: x.name, detail: `backend · ${x.language} · :${x.port} · ${x.description}`})),
        ...S.programs.filter(p => p.kind === 'program').map(p => ({value: p.name, detail: `program · :${p.port} · ${p.description}`})),
      ],
      toolkit: S.toolkits.map(x => ({value: x.name, detail: x.builtin ? 'built-in' : x.installed ? 'installed' : 'available'})),
      target: [
        ...liveServices.map(j => ({value: j.name, detail: `job ${j.id} · :${j.port}`})),
        ...baseTargets.map(bName => {
          const instances = liveServices.filter(j => isInstance(j.name, bName));
          return {value: bName, detail: `${instances.length} instance${instances.length === 1 ? '' : 's'} (${instances.map(j => j.name).join(', ')})`};
        }),
        ...S.external.map(e => ({value: e.name, detail: `:${e.port} · started outside`})),
      ],
      job: [...new Set(S.jobs.map(j => j.name))].map(n => ({value: n, detail: jobBy(n)!.state})),
      ppk: ppk.map(f => ({value: f, detail: 'in this folder'})),
    },
  };
}

/** Folders and .ppk files in `dir`, for completing a path (`run ../examples/ca`). */
function listDir(dir: string): Choice[] {
  try {
    const at = dir.replace(/^~(?=\/|$)/, homedir()) || '.';
    return readdirSync(at, {withFileTypes: true})
      .filter(e => !e.name.startsWith('.') && (e.isDirectory() || e.name.endsWith('.ppk')))
      .map(e => ({value: e.isDirectory() ? `${e.name}/` : e.name, detail: e.isDirectory() ? 'folder' : 'package'}));
  } catch {
    return [];
  }
}

/* ---------- history file ---------- */

const historyFile = join(process.env.XDG_STATE_HOME || join(homedir(), '.local', 'state'), 'paperboat', 'history');

function loadHistory(): void {
  try {
    S.history = readFileSync(historyFile, 'utf8').split('\n').filter(Boolean).slice(-1000);
  } catch {
    S.history = [];
  }
}

function saveHistory(line: string): void {
  try {
    mkdirSync(join(historyFile, '..'), {recursive: true});
    appendFileSync(historyFile, `${line}\n`);
  } catch {
    // history is a convenience: a read-only home just doesn't keep it
  }
}

/* ---------- start-up and polling ---------- */

interface Running {
  port: number;
  pid: number;
  domain: string;
  kind?: 'backend' | 'app';
  name?: string;
}

export async function refreshBackends(): Promise<void> {
  S.backends = (await pb.json<Backend[]>(['backends', '--json'])) ?? S.backends;
  changed();
}

export async function refreshPrograms(): Promise<void> {
  S.programs = (await pb.json<Program[]>(['pkg', 'list', '--json'])) ?? S.programs;
  changed();
}

/** squero-studio-om, or its command (squero): something `start` runs that is not a backend. */
export function programNamed(target: string): Program | undefined {
  return S.programs.find(p => p.kind === 'program' && (p.name === target || (p.launch !== '' && p.launch === target)));
}

export async function refreshToolkits(): Promise<void> {
  S.toolkits = (await pb.json<Toolkit[]>(['toolkit', 'list', '--json'])) ?? S.toolkits;
  changed();
}

async function poll(): Promise<void> {
  const list = await pb.json<Running[]>(['list', '--json']);
  if (list) {
    for (const job of S.jobs) {
      if (job.kind === 'build' || job.state !== 'building') continue;
      const rec = job.port ? list.find(r => r.port === job.port && r.pid !== job.stale) : undefined;
      if (rec) ready(job, rec.pid);
    }
    S.external = list
      .filter(r => !S.jobs.some(j => alive(j) && j.port === r.port))
      .map(r => ({
        name: r.kind === 'app' && r.name ? r.name : r.domain && r.domain !== 'unknown' ? r.domain : (S.backends.find(x => x.port === r.port)?.name ?? `:${r.port}`),
        port: r.port,
        pid: r.pid,
        kind: r.kind ?? 'backend',
      }));
    changed();
  }
  setTimeout(poll, S.jobs.some(j => j.state === 'building') ? 700 : 2500).unref();
}

function git(args: string[]): Promise<string> {
  return new Promise(resolve => execFile('git', args, (err, out) => resolve(err ? '' : out.trim())));
}

export async function init(): Promise<void> {
  loadHistory();
  banner();
  const commands = await pb.json<CommandSpec[]>(['commands', '--json']);
  if (commands) {
    const list = commands.find(c => c.name === 'list');
    if (list) list.flags.push('-i');
    S.commands = [...commands, ...OWN];
  } else {
    say([dg(`could not run ${pb.BIN} commands --json`), mu(' · set PAPERBOAT_BIN to the paperboat binary')]);
  }
  void poll();
  const [branch, hash, ver] = await Promise.all([
    git(['rev-parse', '--abbrev-ref', 'HEAD']),
    git(['rev-parse', '--short', 'HEAD']),
    pb.run(['--version']),
  ]);
  S.git = branch && hash ? `${branch} @ ${hash}` : hash;
  S.version = ver.out[0]?.replace(/^paperboat\s+/, '') ?? '';
  await Promise.all([refreshBackends(), refreshToolkits(), refreshPrograms()]);
}

/* ---------- banner ---------- */

export function banner(): void {
  const W = (t: string) => s(t);
  const Sh = (t: string) => fa(t);
  const boat: Out[] = [
    [W('          ▄█'), Sh('▄'), W('          ')],
    [W('        ▄███'), Sh('██▄'), W('        ')],
    [W('      ▄█████'), Sh('████▄'), W('      ')],
    [W('▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄')],
    [W(' ▀███████████████████▀ ')],
    [W('   ▀███████████████▀   ')],
  ];
  const cwd = process.cwd().replace(homedir(), '~');
  const text: Out[] = [[b('Paperboat console')], [mu('C++20 core · protocol paperboat/0')], [mu(cwd)], [], [], []];
  boat.forEach((l, i) => say([s('  '), ...l, s('   '), ...text[i]]));
  say([s('  '), sg('███  ███  ███  ███  ███')]);
  say([]);
  say([mu('Type a command, or '), s('help'), mu('. Tab completes, → takes the grey suggestion.')]);
  say([]);
}

export function clearScreen(): void {
  process.stdout.write('\x1b[2J\x1b[3J\x1b[H');
  S.transcript = [];
  S.clears++;
  banner();
}

/* ---------- jobs ---------- */

let nextJob = 1;

/** What a job is busy with before it's up: C++ backends compile first. */
export function doing(job: Job): string {
  if (job.kind === 'build') return 'compiling';
  const base = parseInstanceName(job.name).base;
  const language = S.backends.find(x => x.name === job.name || x.name === base)?.language;
  return job.kind === 'backend' && language !== 'python' ? 'building' : 'starting';
}

function startJob(name: string, kind: Job['kind'], args: string[], port: number): Job {
  const job: Job = {
    id: nextJob++, name, kind, args, port, pid: 0, state: 'building', code: null, stopped: false,
    t0: Date.now(), ready: 0, stale: S.external.find(e => e.port === port)?.pid ?? 0, log: [], child: null,
  };
  S.jobs.push(job);
  say([sg('▸ '), b(name), mu(' · '), s(doing(job)), mu(` · job ${job.id} · logs ${name} to follow`)]);
  spawnJob(job);
  return job;
}

function spawnJob(job: Job): void {
  job.state = 'building';
  job.t0 = Date.now();
  job.stopped = false;
  job.code = null;
  const child = pb.start(
    job.args,
    line => {
      pushLine(job.log, line);
      // `paperboat run` names the port it chose (descriptor, manifest or --port).
      const port = /^paperboat running .* on ws:\/\/[^:]+:(\d+)/.exec(line);
      if (port && job.kind === 'backend') job.port = Number(port[1]);
      if (job.kind === 'web' && job.state === 'building' && /ready in|Local:/.test(line)) ready(job, job.pid);
      changed();
    },
    true,
  );
  job.child = child;
  job.pid = child.pid ?? 0;
  void pb.exited(child).then(code => {
    if (job.child !== child) return;  // restarted meanwhile
    job.child = null;
    job.state = 'exited';
    job.code = code;
    if (job.stopped) {
      // stopJob already said so
    } else if (job.kind === 'build' && code === 0) {
      say([b('✓'), s(' built '), b(job.name), mu(` in ${sec(Date.now() - job.t0)}`)]);
    } else if (code === 0) {
      say([mu('■ '), b(job.name), mu(' exited')]);
    } else {
      say([dg(`✗ ${job.name} exited ${code}`), mu(' · last lines')]);
      for (const l of job.log.slice(-5)) say([fa('  │ '), /error|fail/i.test(l) ? dg(l) : mu(l)]);
      say([mu(`  logs ${job.name} for the full log`)]);
    }
    changed();
  });
  changed();
}

function ready(job: Job, pid: number): void {
  job.state = 'running';
  job.ready = Date.now();
  if (pid) job.pid = pid;
  const url = job.kind === 'web' ? `http://localhost:${job.port}` : `ws://localhost:${job.port}`;
  say([sg('● '), b(job.name), s(' ready '), mu('· '), s(url), mu(` · pid ${job.pid} · ${sec(job.ready - job.t0)}`)]);
}

function stopJob(job: Job, quiet = false): Promise<void> {
  const child = job.child;
  job.stopped = true;
  if (!quiet) say([mu('■ '), s('stopped '), b(job.name), mu(job.port ? ` :${job.port}` : '')]);
  if (!child) {
    job.state = 'exited';
    return Promise.resolve();
  }
  child.kill('SIGTERM');
  const kill = setTimeout(() => child.kill('SIGKILL'), 4000);
  return pb.exited(child).then(() => clearTimeout(kill));
}

async function restartJob(job: Job): Promise<void> {
  say([sg('↻ '), s('restarting '), b(job.name)]);
  await stopJob(job, true);
  job.stale = job.pid;  // until the old record is gone, the port still lists the old process
  spawnJob(job);
}

/* ---------- tasks ---------- */

let nextTask = 1;

function newTask(kind: Task['kind'], title: string): Task {
  const task: Task = {id: nextTask++, kind, title, t0: Date.now(), steps: [], progress: {files: 0, total: 0, bytes: 0}, ops: []};
  S.tasks.push(task);
  changed();
  return task;
}

function endTask(task: Task): void {
  S.tasks = S.tasks.filter(t => t !== task);
  changed();
}

const plainTone = (line: string, stream: string): Seg =>
  stream === 'stderr' || /^(error|\[fail)/i.test(line) ? (/error|fail/i.test(line) ? dg(line) : mu(line)) : s(line);

/** A one-shot command: its lines land in the transcript as they come; the prompt stays free. */
async function plain(args: string[]): Promise<number> {
  const task = newTask('plain', args.join(' '));
  const {code} = await pb.run(args, (line, stream) => say([s('  '), plainTone(line, stream)]));
  endTask(task);
  if (code !== 0) say([dg(`✗ ${args[0]} exited ${code}`)]);
  return code;
}

export function stepLine(step: Step, now = Date.now()): Out {
  const glyph =
    step.state === 'done' ? b('✓') : step.state === 'failed' ? dg('✗') : step.state === 'skipped' ? fa('–') : fa('·');
  const msg =
    step.state === 'waiting' ? fa('waiting') : step.state === 'skipped' ? fa(step.message) :
    step.state === 'failed' ? dg(step.message) : step.state === 'running' ? sg('running…') : s(step.message);
  const time = step.state === 'running' ? sec(now - step.t0) : step.duration > 0.001 ? `${step.duration.toFixed(2)}s` : '';
  return [s('    '), glyph, s(' '), s(step.name.padEnd(10)), msg, mu(time ? `  ${time}` : '')];
}

async function check(args: string[]): Promise<void> {
  const target = positional(args)[0] ?? '.';
  const task = newTask('check', target);
  task.steps = ['manifest', 'catalog', 'smoke', 'golden'].map(name => ({name, state: 'waiting', message: '', t0: 0, duration: 0}));
  let result: Record<string, unknown> | null = null;
  const other: string[] = [];
  await pb.events(
    ['check', ...args, '--json-stream'],
    e => {
      const step = task.steps.find(x => x.name === e.name);
      if (e.event === 'step-start' && step) Object.assign(step, {state: 'running', t0: Date.now()});
      else if (e.event === 'step' && step) {
        Object.assign(step, {
          state: e.skipped ? 'skipped' : e.ok ? 'done' : 'failed',
          message: String(e.message ?? ''),
          duration: Number(e.duration ?? 0),
        });
      } else if (e.event === 'result') result = e;
      changed();
    },
    line => other.push(line),
  );
  endTask(task);
  const r = result as null | {ok: boolean; name: string; language: string; version: string; solver?: string; errors: string[]; warnings: string[]};
  say([b('check '), s(r?.name || target), mu(r ? ` · ${r.language} · ${r.version}${r.solver ? ` · solver: ${r.solver}` : ''}` : '')]);
  for (const step of task.steps) if (step.state !== 'waiting') say(stepLine(step));
  for (const w of r?.warnings ?? []) say([mu(`    warning: ${w}`)]);
  if (r?.ok) {
    const total = task.steps.reduce((a, x) => a + x.duration, 0);
    say([b('✓'), s(' verification passed'), mu(` · ${args.includes('--fast') ? 'fast check' : 'all stages'} · ${total.toFixed(1)}s`)]);
  } else {
    say([dg('✗ verification failed')]);
    for (const e of r?.errors ?? other.slice(-5)) say([fa('  │ '), dg(e)]);
  }
}

async function packOrUnpack(kind: 'pack' | 'unpack', args: string[]): Promise<void> {
  const target = positional(args)[0] ?? '.';
  const task = newTask(kind, target);
  let result: Record<string, unknown> = {};
  const other: string[] = [];
  await pb.events(
    [kind, ...args, '--json-stream'],
    e => {
      if (e.event === 'progress') task.progress = {files: Number(e.files), total: Number(e.total), bytes: Number(e.bytes)};
      else if (e.event === 'result') result = e;
      changed();
    },
    line => other.push(line),
  );
  endTask(task);
  const elapsed = sec(Date.now() - task.t0);
  if (result.ok && kind === 'pack') {
    const name = String(result.package ?? '').split('/').pop();
    say([b('✓'), s(' packed '), b(name ?? ''), mu(` · ${result.files} files · ${kb(Number(result.bytes))} · ${elapsed}`)]);
    say([mu(`  sha256 ${result.sha256}`)]);
  } else if (result.ok) {
    say([b('✓'), s(' unpacked '), b(`${result.name} ${result.version}`), mu(` · ${result.files} files verified · ${result.dir}`)]);
  } else {
    say([dg(`✗ ${kind} failed`)]);
    for (const e of (result.errors as string[] | undefined) ?? other.slice(-5)) say([fa('  │ '), dg(e)]);
  }
}

export const kb = (bytes: number) => (bytes >= 1e6 ? `${(bytes / 1e6).toFixed(1)} MB` : `${(bytes / 1e3).toFixed(1)} KB`);

/** Toolkit changes, one after another, each its own row with a spinner. */
export async function toolkitOps(ops: Array<{op: string; name: string}>): Promise<void> {
  const validOps = ops.filter(o => o.name !== 'builtin' && !S.toolkits.find(t => t.name === o.name)?.builtin);
  if (!validOps.length) {
    say([dg('cannot modify built-in toolkit')]);
    return;
  }
  const task = newTask('toolkit', 'toolkits');
  task.ops = validOps.map(o => ({...o, state: 'queued', t0: 0, log: []}));
  for (const op of task.ops) {
    op.state = 'running';
    op.t0 = Date.now();
    changed();
    const {code} = await pb.run(['toolkit', op.op, op.name], line => {
      pushLine(op.log, line);
      changed();
    });
    op.state = code === 0 ? 'done' : 'failed';
  }
  endTask(task);
  for (const op of task.ops) {
    const verb = {install: 'installed', uninstall: 'removed', update: 'updated'}[op.op] ?? op.op;
    if (op.state === 'done') say([b('✓ '), s(`${verb} `), b(op.name), mu(` · ${sec(Date.now() - op.t0)}`)]);
    else {
      say([dg(`✗ toolkit ${op.op} ${op.name} failed`)]);
      for (const l of op.log.slice(-4)) say([fa('  │ '), mu(l)]);
    }
  }
  await refreshToolkits();
}

/* ---------- commands ---------- */

// Flags that take a value: the word after one is not a positional argument.
const VALUED = new Set(['--port', '--var', '--out', '--dir', '--host', '--version', '-v', '-n']);

export function positional(args: string[]): string[] {
  const out: string[] = [];
  for (let i = 0; i < args.length; i++) {
    if (VALUED.has(args[i])) i++;
    else if (!args[i].startsWith('-')) out.push(args[i]);
  }
  return out;
}

const flag = (args: string[], name: string) => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

/** Backends matching the picker's filter, best first. */
export function pickList(query: string): Array<{backend: Backend; match: number[]}> {
  return S.backends
    .flatMap(backend => {
      const m = fuzzy(query, backend.name);
      return m ? [{backend, match: m.match, score: m.score}] : [];
    })
    .sort((x, y) => y.score - x.score);
}

/** The dashboard's rows: this console's jobs (finished builds dropped), then the others. */
export function dashRows(): Array<{job?: Job; ext?: External}> {
  return [...S.jobs.filter(j => j.kind !== 'build' || alive(j)).map(job => ({job})), ...S.external.map(ext => ({ext}))];
}

export function openPicker(): void {
  S.mode = 'picker';
  S.picker = {query: '', i: 0};
  void refreshBackends();
}

export function openToolkits(): void {
  S.mode = 'toolkit';
  S.tk = {i: 0, want: S.toolkits.map(t => t.installed)};
  void refreshToolkits().then(() => {
    if (S.mode === 'toolkit') S.tk.want = S.toolkits.map(t => t.installed);
  });
}

export function openDash(): void {
  S.mode = 'dash';
  S.dash = {i: 0};
}

export function openLogs(job: Job, back: Mode): void {
  S.mode = 'logs';
  S.logs = {job: job.id, back};
}

export function runBackend(target: string, args: string[] = []): void {
  const backend = S.backends.find(x => x.name === target);
  const baseName = backend ? backend.name : targetName(target);
  const liveJobs = S.jobs.filter(j => alive(j) && j.kind !== 'build');
  const name = nextInstanceName(baseName, liveJobs.map(j => j.name));

  const explicitPortStr = flag(args, '--port');
  const explicitPort = explicitPortStr ? Number(explicitPortStr) : 0;
  if (explicitPort) {
    const conflict = liveJobs.find(j => j.port === explicitPort);
    if (conflict) {
      say([mu(`${conflict.name} is already running on :${conflict.port} · choose another --port or stop ${conflict.name}`)]);
      return;
    }
  }

  const usedPorts = new Set<number>([
    ...liveJobs.map(j => j.port).filter(Boolean),
    ...S.external.map(e => e.port),
  ]);
  let port = explicitPort || backend?.port || 8765;
  if (!explicitPort) {
    while (usedPorts.has(port)) port++;
  }

  const restArgs = args.filter(a => a !== target);
  const runArgs = !explicitPort && port ? ['run', target, ...restArgs, '--port', String(port)] : ['run', target, ...restArgs];
  startJob(name, 'backend', runArgs, port);
}

export function stopTarget(target: string): void {
  const jobs = servicesBy(target);
  if (jobs.length) {
    for (const job of jobs) void stopJob(job);
  } else {
    void plain(['stop', target]);
  }
}

export function restartTarget(target: string): void {
  const jobs = servicesBy(target);
  if (jobs.length) {
    for (const job of jobs) void restartJob(job);
  } else {
    void plain(['restart', target]);
  }
}

export function restartJobById(job: Job): void {
  void restartJob(job);
}

export function stopJobById(job: Job): void {
  if (alive(job)) void stopJob(job);
}

function help(): void {
  for (const c of S.commands) {
    say([s(`  ${c.name.padEnd(10)}`), mu(c.summary), fa(c.aliases.length ? ` · ${c.aliases.join(', ')}` : '')]);
  }
  say([mu('  keys: tab complete · → take suggestion · ↑↓ history · ctrl+r search · ctrl+c clear, twice to quit')]);
}

function table(header: string, rows: Out[]): void {
  say([mu(header)]);
  rows.forEach(say);
}

function ps(): void {
  const live = S.jobs.filter(j => alive(j) && j.kind !== 'build');
  if (!live.length && !S.external.length) {
    say([mu('nothing running · start runs the web app, start <backend|program> the rest')]);
    return;
  }
  table(`  ${'STATE'.padEnd(12)}${'KIND'.padEnd(9)}${'NAME'.padEnd(20)}${'PORT'.padEnd(7)}${'PID'.padEnd(9)}FROM`, [
    ...live.map(j => [
      s('  '), j.state === 'running' ? sg('● running   ') : sg('◌ building  '), mu((j.kind === 'backend' ? 'backend' : 'app').padEnd(9)),
      s(j.name.padEnd(20)), s(String(j.port).padEnd(7)), s(String(j.pid).padEnd(9)), mu('console'),
    ]),
    ...S.external.map(e => [
      s('  '), mu('● running   '), mu(e.kind.padEnd(9)), s(e.name.padEnd(20)), s(String(e.port).padEnd(7)), s(String(e.pid).padEnd(9)), mu('outside'),
    ]),
  ]);
}

export function requestExit(): void {
  if (S.jobs.some(alive)) S.confirm = true;
  else S.quit = true;
  changed();
}

export async function exitNow(): Promise<void> {
  S.confirm = false;
  const live = S.jobs.filter(alive);
  if (live.length) say([mu(`stopping ${live.map(j => j.name).join(', ')}…`)]);
  await Promise.all(live.map(j => stopJob(j, true)));
  S.quit = true;
  changed();
}

/** The web app: `start`, `stop` and `restart` without a target, and `app <verb>`. */
function appCommand(verb: string, rest: string[]): void {
  const web = S.jobs.find(j => j.kind === 'web' && j.name === 'web' && alive(j));
  const port = Number(flag(rest, '--port')) || 3047;
  if (verb === 'start') {
    if (web) return void say([mu(`web is already running on :${web.port} · restart restarts it`)]);
    startJob('web', 'web', ['app', 'start', ...rest], port);
  } else if (verb === 'stop') {
    if (web) void stopJob(web);
    else void plain(['app', 'stop', ...rest]);
  } else if (verb === 'restart') {
    if (web) void restartJob(web);
    else startJob('web', 'web', ['app', 'start', ...rest], port);
  } else if (verb === 'build') {
    startJob('web-build', 'build', ['app', 'build'], 0);
  } else {
    say([mu('start | stop | restart (the web app) · app build')]);
  }
}

/** Run one line typed at the prompt. */
export function exec(raw: string): void {
  const text = raw.trim();
  say([sg('› '), s(text)]);
  if (!text) return;
  S.history = remember(S.history, text);
  S.hi = -1;
  saveHistory(text);

  const [word, ...args] = splitWords(text);
  const spec = findCommand(S.commands, word);
  const pos = positional(args);
  if (!spec) {
    const best = S.commands
      .map(c => ({c, m: fuzzy(word, c.name)}))
      .filter(x => x.m)
      .sort((x, y) => y.m!.score - x.m!.score)[0];
    say([dg(`unknown command "${word}"`), mu(best ? ` · did you mean ${best.c.name}?` : ' · help lists commands')]);
    return;
  }
  switch (spec.name) {
    case 'help':
      return help();
    case 'clear':
      return clearScreen();
    case 'exit':
      return requestExit();
    case 'run':
    case 'start': {
      if (!pos[0]) return spec.name === 'run' ? openPicker() : appCommand('start', args);  // start alone: the web app
      const program = programNamed(pos[0]);  // run on a program starts it, as the picker offers
      if (!program) return runBackend(pos[0], args);
      if (program.installed === false) say([mu(`${program.name} is not installed yet · it is fetched and built first`)]);
      const port = Number(flag(args, '--port')) || program.port;
      if (S.external.some(e => e.port === port) || S.jobs.some(j => alive(j) && j.port === port)) {
        return void say([mu(`${program.name} is already running on :${port} · stop ${program.name} first`)]);
      }
      startJob(program.name, 'web', ['start', program.name, ...args.filter(a => a !== pos[0])], port);
      return;
    }
    case 'build':
    case 'rebuild': {
      const target = pos[0] ?? '.';
      startJob(target === '.' ? 'build' : target, 'build', [spec.name, ...args], 0);
      return;
    }
    case 'logs': {
      const job = jobBy(pos[0] ?? '');
      if (!job) {
        say([dg(`no job named "${pos[0] ?? ''}"`), mu(' · logs are kept for jobs started in this console')]);
        return;
      }
      if (args.includes('-f')) return openLogs(job, 'prompt');
      for (const l of job.log.slice(-(Number(flag(args, '-n')) || 20))) say([fa('  │ '), mu(l)]);
      return;
    }
    case 'list':
      return args.includes('-i') ? openDash() : ps();
    case 'top':
      return openDash();
    case 'stop':
      if (args.includes('--all')) {
        S.jobs.filter(alive).forEach(j => void stopJob(j));
        void plain(['stop', '--all']);
        return;
      }
      if (!pos[0]) return appCommand('stop', args);
      return stopTarget(pos[0]);
    case 'restart':
      if (!pos[0]) return appCommand('restart', args);
      return restartTarget(pos[0]);
    case 'app':
      return appCommand(pos[0] ?? '', args.filter(x => x !== pos[0]));
    case 'toolkit': {
      if (!pos[0]) return openToolkits();
      if (['install', 'add', 'update', 'uninstall', 'remove'].includes(pos[0]) && !args.includes('--version')) {
        const op = pos[0] === 'add' ? 'install' : pos[0] === 'remove' ? 'uninstall' : pos[0];
        const names = op === 'update' && args.includes('--all') ? S.toolkits.filter(t => t.installed && !t.builtin).map(t => t.name) : pos.slice(1);
        if (!names.length) return void say([dg('which toolkit?'), mu(' · toolkit list shows them')]);
        void toolkitOps(names.map(name => ({op, name})));
        return;
      }
      void plain([word, ...args]);
      return;
    }
    case 'check':
      void check(args);
      return;
    case 'pack':
    case 'unpack':
      void packOrUnpack(spec.name, args);
      return;
    default: {
      const refresh = spec.name === 'backends';
      void plain([word, ...args]).then(() => (refresh ? refreshBackends() : undefined));
    }
  }
}
