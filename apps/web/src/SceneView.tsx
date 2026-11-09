import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import type { Entity, View } from "./protocol";
import { meshGeometry, meshLook, paintField } from "./meshes";
import { viewCube } from "@paperboat/shared/viewCube";

/** `look` is what the geometry and material were built from; when it changes, rebuild. */
type World = { scene: THREE.Scene; objects: Map<string, { obj: THREE.Object3D; look: string }> };

const lookOf = (e: Entity) => JSON.stringify([e.shape, e.size, e.color, e.src, e.shape === "mesh" ? meshLook(e) : null]);

function dispose(obj: THREE.Object3D) {
  if (obj instanceof THREE.Mesh || obj instanceof THREE.Line) {
    obj.geometry.dispose();
    const material = obj.material as THREE.MeshBasicMaterial;
    material.map?.dispose();
    material.dispose();
  }
}

function createObject(e: Entity, flat = false): THREE.Object3D {
  // An entity with no colour of its own takes the theme's muted ink.
  const color = new THREE.Color(e.color ?? (getComputedStyle(document.documentElement).getPropertyValue("--text-muted").trim() || "#888888"));
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
  if (e.shape === "mesh") {
    // A mesh from typed arrays (binary frames, #18), coloured by its field when it has one.
    const geometry = meshGeometry(e);
    const field = geometry.getAttribute("color") !== undefined;
    const options = { color: field ? 0xffffff : color, vertexColors: field, side: THREE.DoubleSide };
    return new THREE.Mesh(geometry, flat ? new THREE.MeshBasicMaterial(options) : new THREE.MeshStandardMaterial(options));
  }
  const [a, b, c] = e.size ?? [1, 1, 1];
  const geometry =
    e.shape === "sphere" ? new THREE.SphereGeometry(a, 24, 16)
    : e.shape === "cylinder" ? new THREE.CylinderGeometry(a, a, b, 24) // radius size[0], length size[1] along y
    : new THREE.BoxGeometry(a, b, c);
  return new THREE.Mesh(geometry, flat ? new THREE.MeshBasicMaterial({ color }) : new THREE.MeshStandardMaterial({ color }));
}

function applyEntity(obj: THREE.Object3D, e: Entity) {
  if (obj instanceof THREE.Line) {
    const attr = obj.geometry.getAttribute("position") as THREE.BufferAttribute;
    attr.setXYZ(0, ...e.pos);
    attr.setXYZ(1, ...(e.to ?? e.pos));
    attr.needsUpdate = true;
  } else {
    if (e.shape === "mesh" && obj instanceof THREE.Mesh) paintField(obj.geometry, e);
    obj.position.set(...e.pos);
    obj.rotation.set(...(e.rot ?? [0, 0, 0]));
    obj.scale.set(...(e.scale ?? [1, 1, 1]));
  }
}

/**
 * Renders the backend's scene entities. Knows shapes, not domains. The camera is fixed
 * at mount from `view`, so the parent remounts this component when the view changes.
 */
export function SceneView({ entities, view, grid, projection = "persp" }: { entities: Record<string, Entity>; view: View; grid: boolean; projection?: "persp" | "iso" }) {
  const host = useRef<HTMLDivElement>(null);
  const world = useRef<World | null>(null);
  // The floor grid (3D only) shows or hides without rebuilding the view.
  const floorGrid = useRef<THREE.GridHelper | null>(null);
  const showGrid = useRef(grid);
  const switchProjection = useRef<((proj: "persp" | "iso") => void) | null>(null);
  useEffect(() => {
    showGrid.current = grid;
    if (floorGrid.current) floorGrid.current.visible = grid;
  }, [grid]);

  useEffect(() => {
    switchProjection.current?.(projection);
  }, [projection]);

  useEffect(() => {
    const el = host.current!;
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    el.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    // The theme's colours (tokens.css); the view is rebuilt when the theme changes.
    const css = getComputedStyle(document.documentElement);
    const token = (name: string, fallback: string) => css.getPropertyValue(name).trim() || fallback;
    scene.background = new THREE.Color(token("--scene-bg", "#e3e3df"));
    // Light is white in both themes: it is physics, not theme.
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
      floor.position.y = view.floor ?? 0;
      floorGrid.current = floor;
      scene.add(floor);
      // The backend may say where the camera opens (and so where Home returns): view.camera.
      const cam = view.camera;
      const fov = cam?.fov ?? 50;
      const eyePos = cam?.eye ?? [2, 4, 8];
      const targetPos = cam?.target ?? [0, 0, 0];
      const initialDist = new THREE.Vector3(...eyePos).distanceTo(new THREE.Vector3(...targetPos));
      const initialH = Math.max(0.1, initialDist * Math.tan((fov * Math.PI) / 360));

      const persp = new THREE.PerspectiveCamera(fov, 1, 0.1, 1000);
      persp.position.set(...eyePos);

      const ortho = new THREE.OrthographicCamera(-initialH, initialH, initialH, -initialH, 0.1, 1000);
      ortho.position.set(...eyePos);

      camera = projection === "iso" ? ortho : persp;

      fit = (aspect) => {
        persp.aspect = aspect;
        persp.updateProjectionMatrix();
        const curDist = controls ? camera.position.distanceTo(controls.target) : initialDist;
        const curH = Math.max(0.1, curDist * Math.tan((fov * Math.PI) / 360));
        ortho.left = -curH * aspect;
        ortho.right = curH * aspect;
        ortho.top = curH;
        ortho.bottom = -curH;
        ortho.updateProjectionMatrix();
      };
    }
    const controls = new OrbitControls(camera, renderer.domElement);
    if (view.mode === "2d") {
      controls.target.set(view.center[0], view.center[1], 0);
      controls.enableRotate = false;
    } else if (view.camera?.target) {
      controls.target.set(...view.camera.target);
    }
    controls.update(); // look at the target before the view cube takes it as home
    world.current = { scene, objects: new Map() };

    if (view.mode === "3d") {
      switchProjection.current = (proj) => {
        const aspect = el.clientWidth / Math.max(el.clientHeight, 1);
        const fov = view.camera?.fov ?? 50;
        const curDist = camera.position.distanceTo(controls.target);
        const curH = Math.max(0.1, curDist * Math.tan((fov * Math.PI) / 360));
        if (proj === "iso" && camera instanceof THREE.PerspectiveCamera) {
          const ortho = new THREE.OrthographicCamera(-curH * aspect, curH * aspect, curH, -curH, 0.1, 1000);
          ortho.position.copy(camera.position);
          ortho.quaternion.copy(camera.quaternion);
          ortho.updateProjectionMatrix();
          camera = ortho;
          controls.object = ortho;
          controls.update();
        } else if (proj === "persp" && camera instanceof THREE.OrthographicCamera) {
          const persp = new THREE.PerspectiveCamera(fov, aspect, 0.1, 1000);
          persp.position.copy(camera.position);
          persp.quaternion.copy(camera.quaternion);
          persp.updateProjectionMatrix();
          camera = persp;
          controls.object = persp;
          controls.update();
        }
      };
    }

    // 3D only: the view cube in the corner (hidden in 2D views).
    const cube = view.mode === "3d" ? viewCube(el, renderer, () => camera, controls, token) : null;
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
      switchProjection.current = null;
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
        entry = { obj: createObject(e, view.mode === "2d"), look: lookOf(e) };
        w.objects.set(e.id, entry);
        w.scene.add(entry.obj);
      }
      applyEntity(entry.obj, e);
    }
  }, [entities]);

  return <div className="scene" ref={host} />;
}
