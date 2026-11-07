#!/usr/bin/env node
// The Paperboat console. `paperboat` (no command) execs this on a terminal, with
// PAPERBOAT_BIN set to itself; `paperboat run|toolkit|top` open it on a picker or panel.
//   node apps/cli/dist/main.js [run|toolkit|top]

import {render} from 'ink';

import {App} from './ui.tsx';

if (!process.stdin.isTTY || !process.stdout.isTTY) {
  console.error('the Paperboat console needs a terminal; use `paperboat <command>` in scripts');
  process.exit(2);
}

const instance = render(<App start={process.argv[2]} />, {exitOnCtrlC: false});
await instance.waitUntilExit();
process.exit(0);
