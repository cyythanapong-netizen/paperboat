// The Station page's body: the station as the backend shows it (its own web page, a graph of
// objects, or a 2D/3D scene), the panel of its objects, and objects dropped from the browser.

import { Suspense, lazy, type MutableRefObject } from "react";
import { DRAG_TYPE, dragging, type Item } from "./Browser";
import type { EngineState } from "./engine";
import type { ClientMsg, ServerMsg, Station } from "./protocol";
import { StationGraph, type StationControls } from "./StationGraph";
import { StationPanel } from "./StationPanel";
import { WebStation } from "./WebStation";
const SceneView = lazy(() => import("./SceneView").then((m) => ({ default: m.SceneView })));

type Props = {
  view: EngineState["stationView"]["view"];
  state: EngineState;
  /** The backend's address, for a station drawn by its own web page. */
  url: string;
  subscribe: (f: (m: ServerMsg) => void) => () => void;
  send: (m: ClientMsg) => void;
  station: Station | null;
  editStation: (next: Station) => void;
  running: boolean;
  stationCanvas: MutableRefObject<StationControls | null>;
  theme: "light" | "dark";
  gridOn: boolean;
  cameraProjection: "persp" | "iso";
  /** Ids already used, on the Station and the Flow. */
  taken: Set<string>;
  addObject: (item: Item) => void;
};

export function StationView({ view, state, url, subscribe, send, station, editStation, running, stationCanvas, theme, gridOn, cameraProjection, taken, addObject }: Props) {
  // A replay puts the Station back at the playhead (#284); live, it is what the backend last sent.
  const entities = state.replay?.active ? state.sceneHistory.entitiesAt(state.replay.time) : state.entities;
  return (
    <div
      className={`station view-${view}`}
      onDragOver={(e) => {
        if (!e.dataTransfer.types.includes(DRAG_TYPE) || dragging.current?.kind !== "object") return;
        e.preventDefault();
        e.dataTransfer.dropEffect = "copy";
      }}
      onDrop={(e) => {
        const it = dragging.current;
        if (!it || it.kind !== "object") return;
        e.preventDefault();
        dragging.current = null;
        addObject(it);
      }}
    >
      {view === "web" && state.stationView.page ? (
        <WebStation
          backend={url}
          page={state.stationView.page}
          state={state}
          subscribe={subscribe}
          send={send}
          onStation={editStation}
        />
      ) : view === "graph" ? (
        <StationGraph objects={state.objects} station={station ?? { objects: [] }} onChange={editStation} locked={running} controls={stationCanvas} />
      ) : (
        <Suspense fallback={null}>
          <SceneView key={`${theme}:${JSON.stringify(state.view)}`} view={state.view} grid={gridOn} projection={cameraProjection} entities={entities} />
        </Suspense>
      )}
      {station && state.objects.length > 0 && (
        <StationPanel
          objects={state.objects}
          station={station}
          taken={taken}
          onChange={editStation}
          locked={running}
        />
      )}
    </div>
  );
}
