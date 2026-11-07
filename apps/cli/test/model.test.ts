import assert from 'node:assert/strict';
import {test} from 'node:test';
import {
  complete,
  edit,
  fuzzy,
  ghost,
  instanceName,
  isInstance,
  isPath,
  nextInstanceName,
  parseInstanceName,
  remember,
  searchHistory,
  splitWords,
  targetName,
  type Sources,
} from '../src/model.ts';

const sources: Sources = {
  commands: [
    {name: 'run', summary: 'run', aliases: ['start'], subcommands: [], flags: ['--port', '--clean'], arg: 'backend'},
    {name: 'restart', summary: 'restart', aliases: [], subcommands: [], flags: [], arg: 'target'},
    {name: 'app', summary: 'app', aliases: [], subcommands: ['start', 'stop', 'build'], flags: [], arg: ''},
    {name: 'toolkit', summary: 'toolkit', aliases: [], subcommands: ['install', 'list'], flags: [], arg: 'toolkit'},
  ],
  args: {
    backend: [
      {value: 'cart-pole', detail: ''},
      {value: 'parts', detail: ''},
      {value: 'parametric', detail: ''},
    ],
    toolkit: [{value: 'opencv', detail: ''}],
  },
};

test('fuzzy ranks prefix over substring over subsequence', () => {
  const p = fuzzy('par', 'parts')!.score;
  const s = fuzzy('art', 'parts')!.score;
  const q = fuzzy('pts', 'parts')!.score;
  assert.ok(p > s && s > q);
  assert.deepEqual(fuzzy('pts', 'parts')!.match, [0, 3, 4]);
  assert.equal(fuzzy('xyz', 'parts'), null);
});

test('complete walks commands, subcommands, flags and argument kinds', () => {
  assert.deepEqual(complete('re', sources).candidates.map(c => c.value), ['restart']);
  assert.deepEqual(complete('st', sources).candidates.map(c => c.value), ['start', 'restart']);
  assert.deepEqual(complete('app st', sources).candidates.map(c => c.value), ['stop', 'start']);
  assert.deepEqual(complete('run --c', sources).candidates.map(c => c.value), ['--clean']);
  assert.deepEqual(complete('run pa', sources).candidates.map(c => c.value), ['parts', 'parametric']);
  assert.deepEqual(complete('toolkit install o', sources).candidates.map(c => c.value), ['opencv']);
  assert.equal(complete('run pa', sources).from, 4);
  assert.deepEqual(complete('nope x', sources).candidates, []);
});

test('ghost prefers history, then the top completion', () => {
  assert.equal(ghost('run c', ['run cart-pole --port 9000'], sources), 'art-pole --port 9000');
  assert.equal(ghost('run c', [], sources), 'art-pole');
  assert.equal(ghost('run zz', [], sources), '');
  assert.equal(ghost('', ['run x'], sources), '');
});

test('history search and remember', () => {
  assert.deepEqual(searchHistory(['check a', 'run b', 'check a', 'check c'], 'check'), ['check c', 'check a']);
  assert.deepEqual(remember(['a'], 'a'), ['a']);
  assert.deepEqual(remember(['a'], '  '), ['a']);
  assert.deepEqual(remember(['a', 'b'], 'c', 2), ['b', 'c']);
});

test('splitWords keeps quoted words together', () => {
  assert.deepEqual(splitWords(`run  x --var "k=a b" ''`), ['run', 'x', '--var', 'k=a b', '']);
});

test('edit moves, inserts and deletes around the cursor', () => {
  let l = {text: '', cursor: 0};
  l = edit(l, {input: 'rn'})!;
  l = edit(l, {input: '', left: true})!;
  l = edit(l, {input: 'u'})!;
  assert.deepEqual(l, {text: 'run', cursor: 2});
  l = edit(l, {input: '', delete: true})!;
  assert.deepEqual(l, {text: 'rn', cursor: 1});
  l = edit({text: 'check parts', cursor: 11}, {input: 'w', ctrl: true})!;
  assert.deepEqual(l, {text: 'check ', cursor: 6});
  assert.equal(edit(l, {input: '\x1b'}), null);
  assert.deepEqual(edit({text: '', cursor: 0}, {input: 'a\tb\n'}), {text: 'a b ', cursor: 4});
});

test('run takes a path: completion lists folders and packages, the job is named for it', () => {
  const dirs: Record<string, Array<{value: string; detail: string}>> = {
    '.': [{value: 'examples/', detail: 'folder'}, {value: 'parts-0.1.0.ppk', detail: 'package'}],
    'examples/': [{value: 'cart-pole-cpp/', detail: 'folder'}, {value: 'chain-python/', detail: 'folder'}],
  };
  const withFs = {...sources, listDir: (d: string) => dirs[d] ?? []};
  assert.ok(isPath('./x') && isPath('a/b') && isPath('~') && !isPath('cart-pole'));
  assert.deepEqual(complete('run examples/c', withFs).candidates.map(c => c.value), ['examples/cart-pole-cpp/', 'examples/chain-python/']);
  assert.deepEqual(complete('run ./', {...withFs, listDir: () => dirs['.']}).candidates.map(c => c.value), ['./examples/', './parts-0.1.0.ppk']);
  assert.equal(ghost('run examples/ca', [], withFs), 'rt-pole-cpp/');
  assert.equal(targetName('../x/cart-pole-cpp/'), 'cart-pole');
  assert.equal(targetName('parts-0.1.0.ppk'), 'parts-0.1.0');
  assert.equal(targetName('/srv/my-backend'), 'my-backend');
});

test('multi-instance backend naming (<name>_<id>) allocates lowest free id and matches base', () => {
  assert.equal(instanceName('cart-pole', 1), 'cart-pole_1');
  assert.deepEqual(parseInstanceName('cart-pole_1'), {base: 'cart-pole', id: 1});
  assert.deepEqual(parseInstanceName('cart-pole_12'), {base: 'cart-pole', id: 12});
  assert.deepEqual(parseInstanceName('cart-pole'), {base: 'cart-pole', id: null});
  assert.deepEqual(parseInstanceName('my_backend_2'), {base: 'my_backend', id: 2});

  assert.ok(isInstance('cart-pole', 'cart-pole'));
  assert.ok(isInstance('cart-pole_1', 'cart-pole'));
  assert.ok(isInstance('cart-pole_2', 'cart-pole'));
  assert.ok(!isInstance('cart-pole-extra_1', 'cart-pole'));
  assert.ok(!isInstance('parts_1', 'cart-pole'));

  // Sequential allocation and gap filling after an instance stops
  assert.equal(nextInstanceName('cart-pole', []), 'cart-pole_1');
  assert.equal(nextInstanceName('cart-pole', ['cart-pole_1']), 'cart-pole_2');
  assert.equal(nextInstanceName('cart-pole', ['cart-pole_1', 'cart-pole_2']), 'cart-pole_3');
  assert.equal(nextInstanceName('cart-pole', ['cart-pole_2']), 'cart-pole_1');
  assert.equal(nextInstanceName('cart-pole', ['parts_1', 'cart-pole_1', 'cart-pole_3']), 'cart-pole_2');
});

test('complete offers distinct <name>_<id> targets for stop and restart', () => {
  const multiSources: Sources = {
    ...sources,
    args: {
      ...sources.args,
      target: [
        {value: 'cart-pole_1', detail: 'job 1 · :8765'},
        {value: 'cart-pole_2', detail: 'job 2 · :8766'},
        {value: 'cart-pole', detail: '2 instances (cart-pole_1, cart-pole_2)'},
      ],
    },
  };
  assert.deepEqual(
    complete('restart cart-pole_', multiSources).candidates.map(c => c.value),
    ['cart-pole_1', 'cart-pole_2'],
  );
  assert.deepEqual(
    complete('restart cart-pole_2', multiSources).candidates.map(c => c.value),
    ['cart-pole_2'],
  );
});

test('servicesBy and jobBy disambiguate <name>_<id> vs base name, and built-in toolkit cannot be toggled', async () => {
  const {S, jobBy, servicesBy} = await import('../src/store.ts');
  const {handleKey} = await import('../src/keys.ts');

  S.jobs = [
    {
      id: 1,
      name: 'cart-pole_1',
      kind: 'backend',
      args: ['run', 'cart-pole', '--port', '8765'],
      port: 8765,
      pid: 1001,
      state: 'running',
      code: null,
      stopped: false,
      t0: 0,
      ready: 1,
      stale: 0,
      log: ['log1'],
      child: null,
    },
    {
      id: 2,
      name: 'cart-pole_2',
      kind: 'backend',
      args: ['run', 'cart-pole', '--port', '8766'],
      port: 8766,
      pid: 1002,
      state: 'running',
      code: null,
      stopped: false,
      t0: 0,
      ready: 2,
      stale: 0,
      log: ['log2'],
      child: null,
    },
  ];

  // Addressing specific instance vs base name
  assert.deepEqual(servicesBy('cart-pole_1').map(j => j.name), ['cart-pole_1']);
  assert.deepEqual(servicesBy('cart-pole_2').map(j => j.name), ['cart-pole_2']);
  assert.deepEqual(servicesBy('8765').map(j => j.name), ['cart-pole_1']);
  assert.deepEqual(servicesBy('cart-pole').map(j => j.name), ['cart-pole_1', 'cart-pole_2']);

  assert.equal(jobBy('cart-pole_1')?.id, 1);
  assert.equal(jobBy('cart-pole_2')?.id, 2);
  assert.equal(jobBy('cart-pole')?.id, 2);

  // Built-in toolkit toggle protection in TUI
  S.toolkits = [
    {name: 'builtin', installed: true, builtin: true, description: 'Built-in'},
    {name: 'opencv', installed: false, builtin: false, description: 'OpenCV'},
  ];
  S.mode = 'toolkit';
  S.tk = {i: 0, want: [true, false]};

  const emptyKey = {
    upArrow: false, downArrow: false, leftArrow: false, rightArrow: false,
    pageDown: false, pageUp: false, home: false, end: false,
    return: false, escape: false, ctrl: false, shift: false, tab: false,
    backspace: false, delete: false, meta: false,
  };
  // Pressing space on builtin does not flip want[0]
  handleKey(' ', emptyKey);
  assert.equal(S.tk.want[0], true);

  // Moving down to opencv and pressing space flips want[1]
  handleKey('', {...emptyKey, downArrow: true});
  handleKey(' ', emptyKey);
  assert.equal(S.tk.want[1], true);

  S.jobs = [];
  S.mode = 'prompt';
});
