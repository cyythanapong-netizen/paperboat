import assert from "node:assert/strict";
import { test } from "node:test";
import * as THREE from "three";
import { floorUnder } from "../src/sceneFloor.ts";
// (geometry is float32, so compare to 1e-6)

const box = (y: number, h: number, rotZ = 0) => {
  const m = new THREE.Mesh(new THREE.BoxGeometry(1, h, 1));
  m.position.y = y;
  m.rotation.z = rotZ;
  return m;
};

test("an empty scene keeps the floor at the origin", () => {
  assert.equal(floorUnder([]), 0);
});

test("the floor goes under a model built below the origin (#263)", () => {
  // cart-pole: rail -0.16..-0.12, cart -0.09..0.13
  assert.ok(Math.abs(floorUnder([box(-0.14, 0.04), box(0.02, 0.22)]) - -0.16) < 1e-6);
  // thermostat: a floor slab at -1.5, 0.2 thick
  assert.ok(Math.abs(floorUnder([box(-1.5, 0.2), box(0.4, 3.8)]) - -1.6) < 1e-6);
});

test("a body in the air keeps the floor at the origin", () => {
  assert.equal(floorUnder([box(2, 1)]), 0);
});

test("rotation and spheres count by what they really cover", () => {
  // a unit box turned 45° reaches sqrt(2)/2 below its centre
  assert.ok(Math.abs(floorUnder([box(0, 1, Math.PI / 4)]) - -Math.SQRT1_2) < 1e-6);
  const ball = new THREE.Mesh(new THREE.SphereGeometry(0.5, 16, 8));
  ball.position.y = -1;
  assert.ok(Math.abs(floorUnder([ball]) - -1.5) < 1e-6);
});
