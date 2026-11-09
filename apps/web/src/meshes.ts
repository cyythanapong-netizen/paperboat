// A mesh entity (shape "mesh") as three.js geometry: its vertices and triangles as buffer
// attributes, and its optional field (values) as vertex colours on a viridis-like scale.
import * as THREE from "three";
import type { Entity } from "./protocol";

// Viridis at five stops, sRGB: low (dark blue) to high (yellow).
const STOPS = [
  [0x44, 0x01, 0x54],
  [0x3b, 0x52, 0x8b],
  [0x21, 0x91, 0x8c],
  [0x5e, 0xc9, 0x62],
  [0xfd, 0xe7, 0x25],
];

const toLinear = (c: number) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

/** Linear RGB per value (what three.js wants for vertex colours), from low to high. */
export function fieldColors(values: number[], range?: [number, number]): Float32Array {
  let [lo, hi] = range && range[1] > range[0] ? range : [Infinity, -Infinity];
  if (!(range && range[1] > range[0]))
    for (const v of values) {
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
  const span = hi > lo ? hi - lo : 1;
  const out = new Float32Array(values.length * 3);
  for (let i = 0; i < values.length; i++) {
    const f = Math.min(1, Math.max(0, (values[i] - lo) / span)) || 0;
    const x = f * (STOPS.length - 1);
    const k = Math.min(STOPS.length - 2, Math.floor(x));
    const t = x - k;
    for (let c = 0; c < 3; c++) out[i * 3 + c] = toLinear((STOPS[k][c] + (STOPS[k + 1][c] - STOPS[k][c]) * t) / 255);
  }
  return out;
}

const asFloat32 = (value: unknown): Float32Array | null => {
  if (value instanceof Float32Array) return value;
  if (!Array.isArray(value)) return null;
  return new Float32Array(value.map(Number));
};

const asIndex = (value: unknown): Uint32Array | null => {
  if (value instanceof Uint32Array) return value;
  if (!Array.isArray(value)) return null;
  return new Uint32Array(value.map(Number));
};

// An id per array object: a mesh is rebuilt when its vertices or triangles are new arrays.
const ids = new WeakMap<object, number>();
let next = 1;
const idOf = (v: unknown) => {
  if (typeof v !== "object" || v === null) return 0;
  let id = ids.get(v);
  if (id === undefined) ids.set(v, (id = next++));
  return id;
};

/** What a mesh's geometry is built from: its arrays' identities, and whether it has a field. */
export const meshLook = (e: Entity) => [idOf(e.vertices), idOf(e.indices), e.values !== undefined];

export function meshGeometry(e: Entity): THREE.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  const vertices = asFloat32(e.vertices);
  if (!vertices) return geometry;
  geometry.setAttribute("position", new THREE.BufferAttribute(vertices, 3));
  const index = asIndex(e.indices);
  if (index) geometry.setIndex(new THREE.BufferAttribute(index, 1));
  geometry.computeVertexNormals();
  if (e.values !== undefined) paintField(geometry, e);
  return geometry;
}

/** Colours the mesh by its field, when the field or its colour range is new. */
export function paintField(geometry: THREE.BufferGeometry, e: Entity) {
  const last = geometry.userData.field as { values?: Entity["values"]; lo?: number; hi?: number } | undefined;
  if (e.values === undefined || (last && last.values === e.values && last.lo === e.range?.[0] && last.hi === e.range?.[1]))
    return;
  geometry.userData.field = { values: e.values, lo: e.range?.[0], hi: e.range?.[1] };
  const values = asFloat32(e.values);
  const n = geometry.getAttribute("position")?.count ?? 0;
  if (!values || values.length !== n) return;
  geometry.setAttribute("color", new THREE.BufferAttribute(fieldColors(Array.from(values), e.range), 3));
}
