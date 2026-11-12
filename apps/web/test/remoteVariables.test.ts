// MX's live variables are not part of the project: a session restored before the field
// existed, or a variables.remote message without a list, must leave an empty list — the
// Variables sheet maps over it on every render.

import assert from "node:assert/strict";
import { test } from "node:test";
import { initial, reduce } from "../src/engine.ts";

test("a variables.remote message replaces the live variables", () => {
  const state = reduce(initial, {
    type: "variables.remote",
    source: "mx",
    variables: [{ name: "k", type: "number", value: 2 }],
  });
  assert.deepEqual(state.remoteVariables, [{ name: "k", type: "number", value: 2 }]);
});

test("a variables.remote message without a list clears them", () => {
  const withRemote = reduce(initial, {
    type: "variables.remote",
    source: "mx",
    variables: [{ name: "k", type: "number", value: 2 }],
  });
  const state = reduce(withRemote, { type: "variables.remote", source: "mx" } as never);
  assert.deepEqual(state.remoteVariables, []);
});

test("a stateless reset restores an empty list", () => {
  assert.deepEqual(reduce(initial, { type: "reset" }).remoteVariables, []);
});
