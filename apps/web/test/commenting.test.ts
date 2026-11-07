import assert from "node:assert/strict";
import { test } from "node:test";
import { chordMatches, describe } from "../src/commands.ts";
import { buildProject, parseProject } from "../src/project.ts";

test("parseProject validates commented, comment_mode, and comment in flow nodes", () => {
  const validProject = buildProject({
    backend: "test",
    flow: {
      nodes: [
        { id: "g1", type: "gain", commented: true, comment_mode: "out", comment: "Gain tuning" },
        { id: "s1", type: "sum", commented: true, comment_mode: "through" },
        { id: "d1", type: "display" },
      ],
      edges: [],
    },
  });

  const parsed = parseProject(JSON.stringify(validProject));
  assert.ok("project" in parsed);
  const nodes = parsed.project.flow.nodes;
  assert.equal(nodes[0].commented, true);
  assert.equal(nodes[0].comment_mode, "out");
  assert.equal(nodes[0].comment, "Gain tuning");

  assert.equal(nodes[1].commented, true);
  assert.equal(nodes[1].comment_mode, "through");
  assert.equal(nodes[1].comment, undefined);

  assert.equal(nodes[2].commented, undefined);
});

test("parseProject rejects invalid comment_mode or non-boolean commented", () => {
  const invalidMode = {
    paperboat: 0,
    backend: "test",
    flow: {
      nodes: [{ id: "g1", type: "gain", commented: true, comment_mode: "invalid_mode" }],
      edges: [],
    },
  };
  const badMode = parseProject(JSON.stringify(invalidMode));
  assert.ok("error" in badMode);
  assert.match(badMode.error, /comment_mode must be "out" or "through"/);

  const invalidCommented = {
    paperboat: 0,
    backend: "test",
    flow: {
      nodes: [{ id: "g1", type: "gain", commented: "yes" }],
      edges: [],
    },
  };
  const badCommented = parseProject(JSON.stringify(invalidCommented));
  assert.ok("error" in badCommented);
  assert.match(badCommented.error, /commented must be a boolean/);

  const invalidComment = {
    paperboat: 0,
    backend: "test",
    flow: {
      nodes: [{ id: "g1", type: "gain", comment: 123 }],
      edges: [],
    },
  };
  const badComment = parseProject(JSON.stringify(invalidComment));
  assert.ok("error" in badComment);
  assert.match(badComment.error, /comment must be a string/);
});

test("comment chords match keyboard shortcuts", () => {
  const commentChord = { key: "/", ctrl: true };
  const bypassChord = { key: "y", ctrl: true, shift: true };

  assert.equal(describe(commentChord), "Ctrl+/");
  assert.equal(describe(bypassChord), "Ctrl+Shift+Y");

  // Ctrl+/ event
  assert.ok(chordMatches(commentChord, { key: "/", ctrlKey: true, metaKey: false, altKey: false, shiftKey: false }));
  // Cmd+/ on Mac
  assert.ok(chordMatches(commentChord, { key: "/", ctrlKey: false, metaKey: true, altKey: false, shiftKey: false }));

  // Ctrl+Shift+Y event
  assert.ok(chordMatches(bypassChord, { key: "y", ctrlKey: true, metaKey: false, altKey: false, shiftKey: true }));
  // Cmd+Shift+Y on Mac
  assert.ok(chordMatches(bypassChord, { key: "Y", ctrlKey: false, metaKey: true, altKey: false, shiftKey: true }));
});
