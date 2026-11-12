import type { InstrumentProps } from "@paperboat/host";
import { SceneView } from "@paperboat/shared/station";

/**
 * The Board's 3D view: the Station's scene itself (objects, meshes, domain entities), drawn with
 * the same SceneView the Station uses. It has no wire of its own: pick it to keep the 3D stage in
 * view while the Flow and Board are running.
 */
export function View3DInstrument({ name, state, params }: InstrumentProps) {
  const entities = state.entities;
  const count = Object.keys(entities).length;
  const grid = params?.grid === undefined ? true : params.grid !== false && params.grid !== 0;
  const view = state.view.mode === "3d" ? { ...state.view, grid } : { mode: "3d" as const, grid };
  return (
    <figure className="instrument view3d">
      <div className="screen view3d-screen">
        {count > 0 ? (
          <SceneView entities={entities} view={view} grid={grid} />
        ) : (
          <span className="view3d-empty">Add an object or a mesh on the Station</span>
        )}
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{count === 1 ? "1 object" : `${count} objects`}</output>
      </figcaption>
    </figure>
  );
}
