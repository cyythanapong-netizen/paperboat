import assert from 'node:assert/strict';
import {test} from 'node:test';
import {complete, edit, fuzzy, ghost, isPath, remember, searchHistory, splitWords, targetName, type Sources} from '../src/model.ts';

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
