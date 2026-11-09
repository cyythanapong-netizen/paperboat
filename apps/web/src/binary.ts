// Binary frames (#18; protocol/README.md "Binary frames"): typed arrays such as a mesh's
// vertices come over the WebSocket as binary messages, each right before the JSON message
// that references it as {"binary": id}. This file reads them, zero-copy where it can: a
// payload starts at a multiple of 8, so a Float32Array (or Float64Array) is a view on the
// frame's own ArrayBuffer. No React here, so the decoder runs under node's test runner.
import type { BinaryFrameDescriptor, DType } from "./protocol";

/** The frame layout this client reads; it asks for it with ?binary=1 in the WebSocket URL. */
export const FRAME_VERSION = 1;
const MAGIC = [0x50, 0x42, 0x49, 0x4e]; // "PBIN"
const HEADER = 12;

export type TypedArray = Float32Array | Float64Array | Int32Array | Uint32Array | Uint8Array;
type ArrayType = {
  new (buffer: ArrayBufferLike, offset?: number, length?: number): TypedArray;
  new (length: number): TypedArray;
  BYTES_PER_ELEMENT: number;
};
const TYPES: Record<DType, ArrayType> = { f32: Float32Array, f64: Float64Array, i32: Int32Array, u32: Uint32Array, u8: Uint8Array };

export type Frame = { descriptor: BinaryFrameDescriptor; data: TypedArray };

const littleEndian = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;

/** Reads one binary message. Throws on anything that isn't a layout-1 frame. */
export function decodeFrame(buffer: ArrayBuffer): Frame {
  const bytes = new Uint8Array(buffer);
  if (bytes.length < HEADER || MAGIC.some((b, i) => bytes[i] !== b)) throw new Error("not a Paperboat binary frame");
  if (bytes[4] !== FRAME_VERSION) throw new Error(`binary frame version ${bytes[4]} is not ${FRAME_VERSION}`);
  if (bytes[5] || bytes[6] || bytes[7]) throw new Error("binary frame flags must be 0");
  const view = new DataView(buffer);
  const length = view.getUint32(8, true);
  if (HEADER + length > bytes.length) throw new Error("binary frame descriptor runs past the frame");
  const descriptor = JSON.parse(new TextDecoder().decode(bytes.subarray(HEADER, HEADER + length))) as BinaryFrameDescriptor;
  if ((descriptor.compression ?? "none") !== "none") throw new Error(`binary frame compression ${descriptor.compression} is not supported`);
  const Type = TYPES[descriptor.dtype];
  if (!Type) throw new Error(`binary frame dtype ${descriptor.dtype} is unknown`);
  const count = descriptor.shape.reduce((a, b) => a * b, 1);
  const start = Math.ceil((HEADER + length) / 8) * 8;
  const size = Type.BYTES_PER_ELEMENT;
  if (start + count * size !== bytes.length) throw new Error(`binary frame ${descriptor.id}: payload is ${bytes.length - start} bytes, not ${count * size}`);
  let data: TypedArray;
  if (littleEndian && start % size === 0) {
    data = new Type(buffer, start, count); // a view: no copy
  } else {
    data = new Type(count); // a big-endian machine reads element by element
    const read = { f32: view.getFloat32, f64: view.getFloat64, i32: view.getInt32, u32: view.getUint32, u8: view.getUint8 }[descriptor.dtype];
    for (let i = 0; i < count; i++) data[i] = read.call(view, start + i * size, true);
  }
  return { descriptor, data };
}

const isRef = (v: unknown): v is { binary: string } =>
  typeof v === "object" && v !== null && !Array.isArray(v) && !ArrayBuffer.isView(v) &&
  typeof (v as { binary?: unknown }).binary === "string" && Object.keys(v).length === 1;

/**
 * Puts each frame's array where `msg` references it ({"binary": id}), in place, and returns
 * the ids it found no frame for. Frames are used up; the caller drops what is left over.
 */
export function resolveRefs(msg: unknown, frames: Map<string, TypedArray>): string[] {
  const missing: string[] = [];
  const walk = (node: unknown): void => {
    if (typeof node !== "object" || node === null || ArrayBuffer.isView(node)) return;
    const entries: [string | number, unknown][] = Array.isArray(node) ? node.map((v, i) => [i, v]) : Object.entries(node);
    for (const [key, value] of entries) {
      if (isRef(value)) {
        const data = frames.get(value.binary);
        if (data) {
          (node as Record<string | number, unknown>)[key] = data;
          frames.delete(value.binary);
        } else missing.push(value.binary);
      } else walk(value);
    }
  };
  walk(msg);
  return missing;
}

/**
 * Receives a connection's messages, text and binary, and gives back whole protocol messages
 * with their arrays in place. A frame belongs to the next text message.
 */
export class FrameReader {
  private pending = new Map<string, TypedArray>();

  /** A binary message: kept for the next text message. */
  frame(buffer: ArrayBuffer) {
    const { descriptor, data } = decodeFrame(buffer);
    this.pending.set(descriptor.id, data);
  }

  /** A text message, parsed, with its arrays in place; `problems`: what didn't fit. */
  message(text: string): { msg: unknown; problems: string[] } {
    const msg = JSON.parse(text) as unknown;
    if (!this.pending.size) return { msg, problems: [] };
    const missing = resolveRefs(msg, this.pending);
    const problems = missing.map((id) => `binary frame ${id} never came`);
    if (this.pending.size) problems.push(`dropped ${this.pending.size} binary frame(s) no message used`);
    this.pending.clear();
    return { msg, problems };
  }

  reset() {
    this.pending.clear();
  }
}

/** `url` asking the backend for binary frames (a backend that doesn't know them ignores it). */
export function withBinary(url: string): string {
  try {
    const u = new URL(url);
    u.searchParams.set("binary", String(FRAME_VERSION));
    return u.toString();
  } catch {
    return url;
  }
}

/**
 * A numeric array as the protocol delivers it (JSON numbers, or a frame's typed array) as a
 * `Type`: the same object when it already is one (no copy), else converted. Null for anything
 * else (absent, or a reference whose frame never came).
 */
export function asTyped<T extends TypedArray>(value: unknown, Type: { new (values: ArrayLike<number>): T }): T | null {
  if (value instanceof Type) return value;
  if (Array.isArray(value) || ArrayBuffer.isView(value)) return new Type(value as ArrayLike<number>);
  return null;
}
