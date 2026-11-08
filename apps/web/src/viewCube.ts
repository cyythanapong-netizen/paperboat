import * as THREE from "three";
import type { OrbitControls } from "three/addons/controls/OrbitControls.js";

/** Side of the cube's corner of the scene, and its distance from the edges, in px. */
const SIZE = 96;
const MARGIN = 8;
const HOME = 26; // the Home key's size
const FACES = ["RIGHT", "LEFT", "TOP", "BOTTOM", "FRONT", "BACK"]; // BoxGeometry's order: +x -x +y -y +z -z
const THIRD = 1 / 3;

/**
 * A view cube in the scene's top-right corner, as in a CAD tool: it shows the camera's
 * orientation. Click a face, an edge or a corner to turn the camera to look from there,
 * drag it to orbit, Home puts the camera back as it was at mount. The cube is a small
 * scene drawn by the same renderer; a transparent overlay takes the pointer, so the
 * scene's own controls never see it. Call `draw` after rendering the scene, `dispose`
 * on unmount.
 */
export function viewCube(
  host: HTMLElement,
  renderer: THREE.WebGLRenderer,
  getCamera: THREE.PerspectiveCamera | THREE.OrthographicCamera | (() => THREE.PerspectiveCamera | THREE.OrthographicCamera),
  controls: OrbitControls,
  token: (name: string, fallback: string) => string,
) {
  const getCam = () => (typeof getCamera === "function" ? getCamera() : getCamera);
  const home = { position: getCam().position.clone(), target: controls.target.clone() };

  // The cube: a label on each face, ink edges.
  const scene = new THREE.Scene();
  const faceTexture = (text: string) => {
    const c = document.createElement("canvas");
    c.width = c.height = 128;
    const g = c.getContext("2d")!;
    g.fillStyle = token("--surface-panel", "#eeeeea");
    g.fillRect(0, 0, 128, 128);
    g.fillStyle = token("--text", "#111111");
    g.font = "600 24px ui-monospace, monospace";
    g.textAlign = "center";
    g.textBaseline = "middle";
    g.fillText(text, 64, 66);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    return t;
  };
  const materials = FACES.map((f) => new THREE.MeshBasicMaterial({ map: faceTexture(f) }));
  const cube = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), materials);
  const edges = new THREE.LineSegments(
    new THREE.EdgesGeometry(cube.geometry),
    new THREE.LineBasicMaterial({ color: token("--line-ink", "#111111") }),
  );
  // Where a click would turn the camera: the third of the cube it is over.
  const mark = new THREE.Mesh(
    new THREE.BoxGeometry(THIRD * 1.04, THIRD * 1.04, THIRD * 1.04),
    new THREE.MeshBasicMaterial({ color: token("--signal", "#ff6a2b"), transparent: true, opacity: 0.55, depthTest: false }),
  );
  mark.visible = false;
  mark.renderOrder = 1;
  scene.add(cube, edges, mark);
  const eye = new THREE.PerspectiveCamera(30, 1, 0.1, 10);

  // The overlay over the cube's corner, and the Home key under it.
  const overlay = document.createElement("div");
  overlay.className = "view-cube";
  overlay.style.cssText = `position:absolute;top:${MARGIN}px;right:${MARGIN}px;width:${SIZE}px;height:${SIZE}px;touch-action:none;`;
  const homeKey = document.createElement("button");
  // A round key with a house, centred under the cube; greyed while the view is already home.
  homeKey.className = "key small view-cube-home at-home";
  homeKey.setAttribute("aria-label", "Home");
  homeKey.title = "Home: back to the opening view";
  homeKey.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M2.5 7.5L8 3l5.5 4.5M4 6.5V13h3V9.5h2V13h3V6.5"/></svg>';
  homeKey.style.cssText = `position:absolute;top:${MARGIN + SIZE + 6}px;right:${MARGIN + (SIZE - HOME) / 2}px;`;
  host.append(overlay, homeKey);

  // Turning the camera: the offset from the target eases to a new direction.
  let anim: { t0: number; from: THREE.Vector3; to: THREE.Vector3; fromTarget: THREE.Vector3; toTarget: THREE.Vector3 } | null = null;
  const reduced = () => matchMedia("(prefers-reduced-motion: reduce)").matches;
  const DURATION = 260;
  const goTo = (position: THREE.Vector3, target: THREE.Vector3) => {
    const from = getCam().position.clone().sub(controls.target);
    const to = position.clone().sub(target);
    if (reduced()) {
      controls.target.copy(target);
      getCam().position.copy(position);
      controls.update();
      return;
    }
    anim = { t0: performance.now(), from, to, fromTarget: controls.target.clone(), toTarget: target.clone() };
  };
  const lookFrom = (dir: THREE.Vector3) => {
    const d = dir.clone().normalize();
    if (Math.abs(d.y) > 0.99) d.z += 0.001; // straight down or up: keep a heading
    const dist = getCam().position.distanceTo(controls.target);
    goTo(controls.target.clone().add(d.normalize().multiplyScalar(dist)), controls.target.clone());
  };
  const step = () => {
    if (!anim) return;
    const p = Math.min((performance.now() - anim.t0) / DURATION, 1);
    const e = p < 0.5 ? 2 * p * p : 1 - Math.pow(-2 * p + 2, 2) / 2;
    const q = new THREE.Quaternion().setFromUnitVectors(anim.from.clone().normalize(), anim.to.clone().normalize());
    const turn = new THREE.Quaternion().slerp(q, e);
    const length = THREE.MathUtils.lerp(anim.from.length(), anim.to.length(), e);
    const offset = anim.from.clone().normalize().applyQuaternion(turn).multiplyScalar(length);
    controls.target.lerpVectors(anim.fromTarget, anim.toTarget, e);
    getCam().position.copy(controls.target).add(offset);
    controls.update();
    if (p === 1) anim = null;
  };

  // Which face, edge or corner is under the pointer: its direction from the cube's centre.
  const ray = new THREE.Raycaster();
  const direction = (e: PointerEvent): THREE.Vector3 | null => {
    const r = overlay.getBoundingClientRect();
    ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -(((e.clientY - r.top) / r.height) * 2 - 1)), eye);
    const hit = ray.intersectObject(cube)[0];
    if (!hit) return null;
    const side = (v: number) => (Math.abs(v) < THIRD / 2 ? 0 : Math.sign(v));
    return new THREE.Vector3(side(hit.point.x), side(hit.point.y), side(hit.point.z));
  };

  let press: { x: number; y: number; moved: boolean } | null = null;
  overlay.addEventListener("pointermove", (e) => {
    if (press) {
      const dx = e.clientX - press.x;
      const dy = e.clientY - press.y;
      if (!press.moved && Math.hypot(dx, dy) < 4) return;
      if (!press.moved) anim = null;
      press = { x: e.clientX, y: e.clientY, moved: true };
      const offset = getCam().position.clone().sub(controls.target);
      const s = new THREE.Spherical().setFromVector3(offset);
      s.theta -= dx * 0.012;
      s.phi = THREE.MathUtils.clamp(s.phi - dy * 0.012, 0.01, Math.PI - 0.01);
      getCam().position.copy(controls.target).add(offset.setFromSpherical(s));
      controls.update();
      return;
    }
    const d = direction(e);
    mark.visible = !!d;
    if (d) mark.position.copy(d).multiplyScalar(THIRD);
    overlay.style.cursor = d ? "pointer" : "";
  });
  overlay.addEventListener("pointerdown", (e) => {
    overlay.setPointerCapture(e.pointerId);
    press = { x: e.clientX, y: e.clientY, moved: false };
  });
  overlay.addEventListener("pointerup", (e) => {
    const was = press;
    press = null;
    if (overlay.hasPointerCapture(e.pointerId)) overlay.releasePointerCapture(e.pointerId);
    if (was && !was.moved) {
      const d = direction(e);
      if (d) lookFrom(d);
    }
  });
  overlay.addEventListener("pointerleave", () => {
    if (!press) mark.visible = false;
  });
  homeKey.addEventListener("click", () => goTo(home.position, home.target));

  return {
    /** Draw the cube over the scene just rendered, `width` x `height` px. */
    draw(width: number, height: number) {
      step();
      const atHome = getCam().position.distanceToSquared(home.position) < 1e-6 && controls.target.distanceToSquared(home.target) < 1e-6;
      if (atHome !== homeKey.classList.contains("at-home")) homeKey.classList.toggle("at-home", atHome);
      // The cube's camera looks at it from where the scene's camera looks at its target.
      eye.quaternion.copy(getCam().quaternion);
      eye.position.set(0, 0, 4).applyQuaternion(getCam().quaternion);
      const x = width - SIZE - MARGIN;
      const y = height - SIZE - MARGIN; // from the bottom edge
      renderer.setViewport(x, y, SIZE, SIZE);
      renderer.setScissor(x, y, SIZE, SIZE);
      renderer.setScissorTest(true);
      renderer.clearDepth();
      renderer.render(scene, eye);
      renderer.setScissorTest(false);
      renderer.setViewport(0, 0, width, height);
    },
    dispose() {
      overlay.remove();
      homeKey.remove();
      for (const m of materials) {
        m.map?.dispose();
        m.dispose();
      }
      cube.geometry.dispose();
      edges.geometry.dispose();
      (edges.material as THREE.Material).dispose();
      mark.geometry.dispose();
      (mark.material as THREE.Material).dispose();
    },
  };
}
