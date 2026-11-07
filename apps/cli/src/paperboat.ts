// Run the C++ `paperboat` CLI: the console never does a backend's work itself, it asks this
// binary (PAPERBOAT_BIN, set by the binary that launched the console) and renders the answer.

import {spawn, type ChildProcess} from 'node:child_process';
import {createInterface} from 'node:readline';

export const BIN = process.env.PAPERBOAT_BIN || 'paperboat';

// Children never open a console of their own, and print plain text (stdout is a pipe).
const ENV = {...process.env, PAPERBOAT_NO_CONSOLE: '1'};

export type OnLine = (line: string, stream: 'stdout' | 'stderr') => void;

/** Start `paperboat <args>` with its lines going to `onLine`. `detached` gives it its own
 * process group, so Ctrl+C in the console never reaches a backend. */
export function start(args: string[], onLine: OnLine, detached = false): ChildProcess {
  const child = spawn(BIN, args, {env: ENV, stdio: ['ignore', 'pipe', 'pipe'], detached});
  createInterface({input: child.stdout!}).on('line', l => onLine(l, 'stdout'));
  createInterface({input: child.stderr!}).on('line', l => onLine(l, 'stderr'));
  return child;
}

/** Resolves with the exit code once the process has exited and its output is drained. */
export function exited(child: ChildProcess): Promise<number> {
  return new Promise(resolve => {
    child.on('error', () => resolve(127));
    child.on('close', (code, signal) => resolve(code ?? (signal ? 128 + 15 : 1)));
  });
}

/** Run to the end and collect the output. */
export async function run(args: string[], onLine?: OnLine): Promise<{code: number; out: string[]; err: string[]}> {
  const out: string[] = [];
  const err: string[] = [];
  const child = start(args, (line, stream) => {
    (stream === 'stdout' ? out : err).push(line);
    onLine?.(line, stream);
  });
  return {code: await exited(child), out, err};
}

/** `paperboat <args> --json`, parsed; null when it fails. */
export async function json<T>(args: string[]): Promise<T | null> {
  const {code, out} = await run(args);
  if (code !== 0) return null;
  try {
    return JSON.parse(out.join('\n')) as T;
  } catch {
    return null;
  }
}

/** `paperboat <args> --json-stream`: each JSON line to `onEvent`, other lines to `onLine`. */
export async function events(
  args: string[],
  onEvent: (e: Record<string, unknown>) => void,
  onLine: OnLine,
): Promise<number> {
  return exited(
    start(args, (line, stream) => {
      if (stream === 'stdout' && line.startsWith('{')) {
        try {
          onEvent(JSON.parse(line));
          return;
        } catch {
          // not an event: fall through to a plain line
        }
      }
      onLine(line, stream);
    }),
  );
}
