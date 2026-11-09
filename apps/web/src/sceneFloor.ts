import * as THREE from "three";

/**
 * Where the 3D floor grid goes when the backend doesn't say (`view.floor`, #263): under the lowest
 * of `objects`, so the grid never cuts through a model built below the origin, and never above 0,
 * so a body that starts in the air (and falls) still has the floor at the origin.
 */
export function floorUnder(objects: Iterable<THREE.Object3D>): number {
  const all = new THREE.Box3();
  const one = new THREE.Box3();
  for (const o of objects) {
    o.updateWorldMatrix(true, true);
    one.setFromObject(o);
    if (!one.isEmpty()) all.union(one);
  }
  return all.isEmpty() ? 0 : Math.min(0, all.min.y);
}
