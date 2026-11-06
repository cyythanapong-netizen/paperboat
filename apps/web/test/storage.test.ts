import assert from "node:assert/strict";
import { test } from "node:test";
import { readStored, writeStored } from "../src/storage.ts";

const isList = (v: unknown): v is number[] => Array.isArray(v);

test("stored JSON round-trips, and bad or missing values read as nothing", () => {
  const box = new Map<string, string>();
  globalThis.localStorage = {
    getItem: (k: string) => box.get(k) ?? null,
    setItem: (k: string, v: string) => void box.set(k, v),
    removeItem: (k: string) => void box.delete(k),
  } as Storage;

  writeStored("k", [1, 2]);
  assert.deepEqual(readStored("k", isList), [1, 2]);
  box.set("k", "{not json");
  assert.equal(readStored("k", isList), undefined);
  box.set("k", '{"a":1}');
  assert.equal(readStored("k", isList), undefined);
  writeStored("k", undefined);
  assert.equal(box.has("k"), false);
  writeStored("k", [3]);
  writeStored("k", null);
  assert.equal(box.has("k"), false);
});

test("blocked storage is quiet: reads find nothing, writes are dropped", () => {
  const blocked = () => {
    throw new Error("SecurityError");
  };
  globalThis.localStorage = { getItem: blocked, setItem: blocked, removeItem: blocked } as unknown as Storage;
  assert.equal(readStored("k", isList), undefined);
  assert.doesNotThrow(() => writeStored("k", [1]));
});
