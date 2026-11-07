import assert from "node:assert/strict";
import { test } from "node:test";
import {
  generateSalt,
  hashPasscode,
  isArrangeLocked,
  isModeAllowed,
  isPageAllowed,
  verifyPasscode,
} from "../src/lock.ts";

test("generateSalt produces a 16-hex character string", () => {
  const s1 = generateSalt();
  const s2 = generateSalt();
  assert.equal(s1.length, 16);
  assert.match(s1, /^[0-9a-f]{16}$/);
  assert.notEqual(s1, s2);
});

test("hashPasscode and verifyPasscode authenticate passwords", async () => {
  const salt = generateSalt();
  const hash = await hashPasscode("secret123", salt);
  assert.equal(hash.length, 64);
  assert.match(hash, /^[0-9a-f]{64}$/);

  const ok = await verifyPasscode("secret123", { salt, hash });
  assert.equal(ok, true);

  const wrong = await verifyPasscode("wrong", { salt, hash });
  assert.equal(wrong, false);

  const wrongCase = await verifyPasscode("Secret123", { salt, hash });
  assert.equal(wrongCase, false);
});

test("isArrangeLocked reports arrange restriction correctly", () => {
  assert.equal(isArrangeLocked(undefined, false), false);
  assert.equal(isArrangeLocked(null, false), false);
  assert.equal(isArrangeLocked({ arrange: false }, false), false);
  assert.equal(isArrangeLocked({ arrange: true }, false), true);
  assert.equal(isArrangeLocked({ arrange: true }, true), false); // unlocked session
});

test("isPageAllowed restricts pages when locked", () => {
  assert.equal(isPageAllowed("flow", undefined, false), true);
  assert.equal(isPageAllowed("flow", { pages: [] }, false), true);
  assert.equal(isPageAllowed("flow", { pages: ["station", "dashboard"] }, false), false);
  assert.equal(isPageAllowed("station", { pages: ["station", "dashboard"] }, false), true);
  assert.equal(isPageAllowed("flow", { pages: ["station", "dashboard"] }, true), true); // unlocked session
});

test("isModeAllowed restricts view modes when locked", () => {
  assert.equal(isModeAllowed("navigator", undefined, false), true);
  assert.equal(isModeAllowed("navigator", { modes: [] }, false), true);
  assert.equal(isModeAllowed("navigator", { modes: ["lookout"] }, false), false);
  assert.equal(isModeAllowed("lookout", { modes: ["lookout"] }, false), true);
  assert.equal(isModeAllowed("navigator", { modes: ["lookout"] }, true), true); // unlocked session
});
