import assert from 'node:assert/strict';
import {test} from 'node:test';

import {LOGO_ROWS, LOGO_WIDTH, logoFrame, logoRow} from '../src/logo.ts';

test('the mark is nine rows of 23 cells at every phase', () => {
  for (const phase of [0, 0.15, 0.3, 0.6, 0.9, 1]) {
    const frame = logoFrame(phase);
    assert.equal(frame.length, LOGO_ROWS);
    for (let row = 0; row < LOGO_ROWS; row++) assert.equal(logoRow(frame, row).length, LOGO_WIDTH, `phase ${phase} row ${row}`);
  }
});

test('at rest the water is one row of five dots and the boat sits in its six rows', () => {
  const rest = logoFrame(1);
  assert.equal(logoRow(rest, 8), '███  ███  ███  ███  ███');
  assert.equal(logoRow(rest, 7).trim(), '');
  assert.equal(logoRow(rest, 0).trim(), '');
  assert.match(logoRow(rest, 6), /█/);
});

test('the crest lifts a dot by half a cell', () => {
  const frame = logoFrame(0.16);
  assert.match(logoRow(frame, 7).slice(0, 3), /▄/);
  assert.match(logoRow(frame, 8).slice(0, 3), /▀/);
});

test('the boat bobs half a row as the crest passes under it', () => {
  const rest = logoFrame(1);
  const bob = logoFrame(0.3);
  assert.equal(logoRow(rest, 0).trim(), '');
  assert.equal(logoRow(bob, 0)[11], '▄');
  assert.equal(logoRow(bob, 0)[12], ' ');
});
