import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import type { Entity, View } from "./protocol";
import { viewCube } from "./viewCube";

/** `look` is what the geometry and material were built from; when it changes, rebuild. */
type World = { scene: THREE.Scene; objects: Map<string, { obj: THREE.Object3D; look: string }> };

const lookOf = (e: Entity) => JSON.stringify([e.shape, e.size, e.color, e.src]);

function dispose(obj: THREE.Object3D) {
  if (obj instanceof THREE.Mesh || obj instanceof THREE.Line) {
    obj.geometry.dispose();
    const material = obj.material as THREE.MeshBasicMaterial;
    material.map?.dispose();
    material.dispose();
  }
}

function createObject(e: Entity): THREE.Object3D {
  const color = new THREE.Color(e.color ?? "#888888");
  if (e.shape === "line") {
    const geometry = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3()]);
    return new THREE.Line(geometry, new THREE.LineBasicMaterial({ color }));
  }
  if (e.shape === "image") {
    const [w, h] = e.size ?? [1, 1];
    const texture = new THREE.TextureLoader().load(e.src ?? "");
    texture.colorSpace = THREE.SRGBColorSpace;
    return new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ map: texture }));
  }
  const [a, b, c] = e.size ?? [1, 1, 1];
  const geometry = e.shape === "sphere" ? new THREE.SphereGeometry(a, 24, 16) : new THREE.BoxGeometry(a, b, c);
  return new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color }));
}

function applyEntity(obj: THREE.Object3D, e: Entity) {
  if (obj instanceof THREE.Line) {
    const attr = obj.geometry.getAttribute("position") as THREE.BufferAttribute;
    attr.setXYZ(0, ...e.pos);
    attr.setXYZ(1, ...(e.to ?? e.pos));
    attr.needsUpdate = true;
  } else {
    obj.position.set(...e.pos);
    obj.rotation.set(...(e.rot ?? [0, 0, 0]));
    obj.scale.set(...(e.scale ?? [1, 1, 1]));
  }
}

/**
 * Renders the backend's scene entities. Knows shapes, not domains. The camera is fixed
 * at mount from `view`, so the parent remounts this component when the view changes.
 */
export function SceneView({ entities, view, grid }: { entities: Record<string, Entity>; view: View; grid: boolean }) {
  const host = useRef<HTMLDivElement>(null);
  const world = useRef<World | null>(null);
  // The floor grid (3D only) shows or hides without rebuilding the view.
  const floorGrid = useRef<THREE.GridHelper | null>(null);
  const showGrid = useRef(grid);
  useEffect(() => {
    showGrid.current = grid;
    if (floorGrid.current) floorGrid.current.visible = grid;
  }, [grid]);

  useEffect(() => {
    const el = host.current!;
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    el.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    // The theme's colours (tokens.css); the view is rebuilt when the theme changes.
    const css = getComputedStyle(document.documentElement);
    const token = (name: string, fallback: string) => css.getPropertyValue(name).trim() || fallback;
    scene.background = new THREE.Color(token("--scene-bg", "#e3e3df"));
    scene.add(new THREE.AmbientLight(0xffffff, 1.2), new THREE.DirectionalLight(0xffffff, 2));

    let camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
    let fit: (aspect: number) => void;
    if (view.mode === "2d") {
      const [cx, cy] = view.center;
      const ortho = new THREE.OrthographicCamera();
      ortho.position.set(cx, cy, 10);
      fit = (aspect) => {
        const h = view.height / 2;
        Object.assign(ortho, { left: -h * aspect, right: h * aspect, top: h, bottom: -h, near: 0.1, far: 100 });
      };
      camera = ortho;
    } else {
      const grid = token("--scene-grid", "#a9a9a3");
      const floor = new THREE.GridHelper(10, 10, grid, grid);
      floor.visible = showGrid.current;
      floorGrid.current = floor;
      scene.add(floor);
      const persp = new THREE.PerspectiveCamera(50, 1, 0.1, 100);
      persp.position.set(2, 4, 8);
      fit = (aspect) => (persp.aspect = aspect);
      camera = persp;
    }
    const controls = new OrbitControls(camera, renderer.domElement);
    if (view.mode === "2d") {
      controls.target.set(view.center[0], view.center[1], 0);
      controls.enableRotate = false;
    }
    world.current = { scene, objects: new Map() };

    // 3D only: the view cube in the corner (hidden in 2D views).
    const cube = camera instanceof THREE.PerspectiveCamera ? viewCube(el, renderer, camera, controls, token) : null;
    renderer.autoClear = false;

    const resize = new ResizeObserver(() => {
      renderer.setSize(el.clientWidth, el.clientHeight);
      fit(el.clientWidth / Math.max(el.clientHeight, 1));
      camera.updateProjectionMatrix();
    });
    resize.observe(el);
    renderer.setAnimationLoop(() => {
      controls.update();
      renderer.clear();
      renderer.render(scene, camera);
      cube?.draw(el.clientWidth, el.clientHeight);
    });

    return () => {
      renderer.setAnimationLoop(null);
      resize.disconnect();
      cube?.dispose();
      controls.dispose();
      renderer.dispose();
      el.removeChild(renderer.domElement);
      world.current = null;
      floorGrid.current = null;
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps -- view is read once; see doc comment

  useEffect(() => {
    const w = world.current;
    if (!w) return;
    for (const [id, { obj, look }] of w.objects) {
      const e = entities[id];
      if (!e || lookOf(e) !== look) {
        w.scene.remove(obj);
        dispose(obj);
        w.objects.delete(id);
      }
    }
    for (const e of Object.values(entities)) {
      let entry = w.objects.get(e.id);
      if (!entry) {
        entry = { obj: createObject(e), look: lookOf(e) };
        w.objects.set(e.id, entry);
        w.scene.add(entry.obj);
      }
      applyEntity(entry.obj, e);
    }
  }, [entities]);

  return <div className="scene" ref={host} />;
}
