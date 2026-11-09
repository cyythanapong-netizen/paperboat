import assert from "node:assert/strict";
import { test } from "node:test";
import { count } from "../src/words.ts";

test("counts say one or many", () => {
  assert.equal(count(1, "block"), "1 block");
  assert.equal(count(0, "block"), "0 blocks");
  assert.equal(count(3, "error"), "3 errors");
  assert.equal(count(2, "category", "categories"), "2 categories");
});
