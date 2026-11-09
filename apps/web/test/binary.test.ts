// Binary frames (#18): the decoder against the spec vector the C++ encoder wrote
// (protocol/vectors/binary-frames-v1.json, from engine/tests/binary_test.cpp).
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { FrameReader, asTyped, decodeFrame, resolveRefs, withBinary, type TypedArray } from "../src/binary.ts";
import { fieldColors } from "../src/meshes.ts";

type Vectors = {
  version: number;
  frames: { name: string; descriptor: { id: string; dtype: string; shape: number[] }; values: number[]; frame: string }[];
  message: { json: unknown; text: unknown; frames: string[] };
};
const vectors = JSON.parse(readFileSync(new URL("../../../protocol/vectors/binary-frames-v1.json", import.meta.url), "utf8")) as Vectors;

/** A hex string as its own ArrayBuffer, as a WebSocket with binaryType "arraybuffer" hands it over. */
const buffer = (hex: string) => {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return bytes.buffer;
};

const TYPES: Record<string, Function> = { f32: Float32Array, f64: Float64Array, i32: Int32Array, u32: Uint32Array, u8: Uint8Array };

test("every dtype of the spec vector decodes to its values, as a view on the frame", () => {
  assert.equal(vectors.version, 1);
  for (const v of vectors.frames) {
    const raw = buffer(v.frame);
    const { descriptor, data } = decodeFrame(raw);
    assert.deepEqual(descriptor, v.descriptor, v.name);
    assert.ok(data instanceof TYPES[v.descriptor.dtype], v.name);
    assert.equal(data.buffer, raw, `${v.name}: zero-copy`);
    assert.equal(data.byteOffset % 8, 0);
    // f32 values are given as their shortest decimals: compare as f32.
    const expected = v.descriptor.dtype === "f32" ? Array.from(new Float32Array(v.values)) : v.values;
    assert.deepEqual(Array.from(data), expected, v.name);
  }
});

test("a scene's frames and text decode to the arrays a JSON-only client gets as numbers", () => {
  const reader = new FrameReader();
  for (const hex of vectors.message.frames) reader.frame(buffer(hex));
  const { msg, problems } = reader.message(JSON.stringify(vectors.message.text));
  assert.deepEqual(problems, []);
  const binary = (msg as { entities: Record<string, unknown>[] }).entities[0];
  const plain = (vectors.message.json as { entities: Record<string, unknown>[] }).entities[0];
  assert.ok(binary.vertices instanceof Float32Array);
  assert.ok(binary.indices instanceof Uint32Array);
  assert.deepEqual(Object.keys(binary).sort(), Object.keys(plain).sort());
  for (const key of Object.keys(plain)) {
    const value = binary[key];
    if (ArrayBuffer.isView(value)) {
      const as = value instanceof Float32Array ? Array.from(new Float32Array(plain[key] as number[])) : plain[key];
      assert.deepEqual(Array.from(value as TypedArray), as, key);
    } else assert.deepEqual(value, plain[key], key);
  }
  // The next message gets no stale frames.
  assert.deepEqual(reader.message('{"type":"log","level":"info","text":"x"}').problems, []);
});

test("refs without frames, and frames no message used, are reported", () => {
  const reader = new FrameReader();
  reader.frame(buffer(vectors.frames[0].frame)); // id 1
  const { msg, problems } = reader.message('{"type":"scene","entities":[{"id":"m","vertices":{"binary":"9"}}]}');
  assert.deepEqual(problems, ["binary frame 9 never came", "dropped 1 binary frame(s) no message used"]);
  assert.deepEqual((msg as { entities: { vertices: unknown }[] }).entities[0].vertices, { binary: "9" });
  // A JSON-only backend's arrays are left alone; an object with other keys isn't a reference.
  const frames = new Map<string, TypedArray>([["1", new Uint8Array([1])]]);
  const other = { a: [1, 2], b: { binary: "1", more: true } };
  assert.deepEqual(resolveRefs(other, frames), []);
  assert.deepEqual(other.b, { binary: "1", more: true });
});

test("a decoder refuses what isn't a layout-1 frame", () => {
  const good = new Uint8Array(buffer(vectors.frames[4].frame));
  const bad = (at: number, value: number) => {
    const copy = good.slice();
    copy[at] = value;
    return copy.buffer;
  };
  assert.throws(() => decodeFrame(bad(0, 0x58)), /not a Paperboat binary frame/);
  assert.throws(() => decodeFrame(bad(4, 2)), /version 2/);
  assert.throws(() => decodeFrame(bad(5, 1)), /flags/);
  assert.throws(() => decodeFrame(good.slice(0, good.length - 1).buffer), /payload/);
});

test("the client asks for binary frames in the URL; numbers come as typed arrays either way", () => {
  assert.equal(withBinary("ws://localhost:8765"), "ws://localhost:8765/?binary=1");
  assert.equal(withBinary("ws://h:1/x?a=b"), "ws://h:1/x?a=b&binary=1");
  const f32 = new Float32Array([1, 2]);
  assert.equal(asTyped(f32, Float32Array), f32); // no copy
  assert.deepEqual(asTyped([1, 2], Float32Array), f32);
  assert.equal(asTyped({ binary: "1" }, Float32Array), null);
});

test("a field's colours run from the low end to the high end of its range", () => {
  const c = fieldColors([0, 1, 0.5, 2], [0, 1]);
  assert.equal(c.length, 12);
  assert.ok(c[2] > c[0] && c[2] > c[1]); // low: blue-violet
  assert.ok(c[3] > 0.9 && c[4] > 0.75 && c[5] < 0.1); // high: yellow
  assert.deepEqual(Array.from(c.slice(9, 12)), Array.from(c.slice(3, 6))); // clamped
  const own = fieldColors([10, 20]); // no range: its own min and max
  assert.deepEqual(Array.from(own.slice(0, 3)), Array.from(c.slice(0, 3)));
});
