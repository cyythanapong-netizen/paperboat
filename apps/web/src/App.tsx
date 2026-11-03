import { useEdgesState, useNodesState, useReactFlow } from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useEngine } from "./engine";
import { FlowEditor, fromGraph, isBlock, portsOf, resolveBlocks, RunStates, syncLinked, toGraph, type FlowNode, type Wire } from "./FlowEditor";
import { absolute, isFrame } from "./groups";
import type { RunSettings, Station } from "./protocol";
import { RunPanel } from "./RunPanel";
import { SceneView } from "./SceneView";
import { StationGraph } from "./StationGraph";
import { StationPanel } from "./StationPanel";
import { RunOptions } from "./RunOptions";
import { useUndo } from "./undo";
import { WebStation } from "./WebStation";

const DEFAULT_URL =
  new URLSearchParams(location.search).get("backend") ??
  import.meta.env.VITE_ENGINE_URL ??
  "ws://localhost:8765";

export function App() {
  const [url, setUrl] = useState(DEFAULT_URL);
  const [draftUrl, setDraftUrl] = useState(DEFAULT_URL);
  const { state, send, subscribe } = useEngine(url);
  const [nodes, setNodes, onNodesChange] = useNodesState<FlowNode>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Wire>([]);
  const { fitView } = useReactFlow();
  const [station, setStation] = useState<Station | null>(null);
  const [runSettings, setRunSettings] = useState<RunSettings>({});
  useEffect(() => setRunSettings({}), [state.engine]); // another backend: its own defaults

  // What the canvas and station were loaded as, and from which backend: a backend that
  // comes back (restarted, reconnected) doesn't overwrite what the user changed since.
  const canvas = useRef({ engine: "", graph: "", station: "" });
  const current = useRef({ nodes, edges, station });
  current.current = { nodes, edges, station };

  // The backend decides what the station opens with (station.load); edits go back to it.
  useEffect(() => {
    const mine = current.current.station;
    const edited = mine && canvas.current.engine === state.engine && JSON.stringify(mine) !== canvas.current.station;
    if (edited) {
      send({ type: "station.set", station: mine }); // keep the user's station; tell the backend
      return;
    }
    setStation(state.station);
    canvas.current.station = JSON.stringify(state.station);
  }, [state.station, state.engine, send]);
  const editStation = useCallback(
    (next: Station) => {
      setStation(next);
      send({ type: "station.set", station: next });
    },
    [send],
  );

  // Linked blocks follow the station: one per object that spawns one.
  useEffect(() => {
    setNodes((ns) => syncLinked(ns, station, state.objects, state.blocks));
  }, [station, state.objects, state.blocks, setNodes]);
  // Wires to a block that's gone (its station object was removed), or to a port it no
  // longer has (a magic block switched form), go too.
  useEffect(() => {
    const ports = new Map<string, Set<string> | null>(
      nodes.map((n) => [n.id, isBlock(n) && !n.data.missing ? new Set([...portsOf(n).inputs, ...portsOf(n).outputs].map((p) => p.id)) : null]),
    );
    const has = (id: string, port: string | null | undefined) => {
      if (!ports.has(id)) return false;
      const p = ports.get(id);
      return p === null || p === undefined || p.has(port ?? "");  // groups and missing blocks keep their wires
    };
    setEdges((es) => {
      const kept = es.filter((e) => has(e.source, e.sourceHandle) && has(e.target, e.targetHandle));
      return kept.length === es.length ? es : kept;
    });
  }, [nodes, setEdges]);

  // The backend decides what the canvas opens with (graph.load), unless the user changed
  // the canvas since this same backend loaded it: then the canvas stays, checked against the
  // (perhaps new) catalog, so a block that's gone shows as missing instead of vanishing.
  useEffect(() => {
    const now = current.current;
    const edited =
      now.nodes.length > 0 &&
      canvas.current.engine === state.engine &&
      JSON.stringify(toGraph(now.nodes, now.edges)) !== canvas.current.graph;
    if (edited) {
      setNodes((ns) => resolveBlocks(ns, now.edges, state.blocks));
      return;
    }
    const g = state.loaded ? fromGraph(state.loaded, state.blocks) : { nodes: [], edges: [] };
    setNodes(g.nodes);
    setEdges(g.edges);
    canvas.current = { ...canvas.current, engine: state.engine, graph: JSON.stringify(toGraph(g.nodes, g.edges)) };
    requestAnimationFrame(() => fitView());
  }, [state.loaded, state.blocks, state.engine, setNodes, setEdges, fitView]);

  // Ctrl+Z / Ctrl+Shift+Z on the Flow canvas; a new graph from the backend starts afresh.
  useUndo(nodes, edges, setNodes, setEdges, state.loaded);

  // Forward the keyboard to the backend (e.g. a Key block). Typing in a field is not input.
  useEffect(() => {
    const forward = (down: boolean) => (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.repeat) return;
      if (e.code === "Space") e.preventDefault(); // don't click the focused button
      send({ type: "input", key: e.code, down });
    };
    const onDown = forward(true);
    const onUp = forward(false);
    window.addEventListener("keydown", onDown);
    window.addEventListener("keyup", onUp);
    return () => {
      window.removeEventListener("keydown", onDown);
      window.removeEventListener("keyup", onUp);
    };
  }, [send]);

  const running = state.run?.status === "running";
  const runStates = useMemo(
    () => ({ active: new Set(state.active), failed: new Set(state.failed), progress: state.progress }),
    [state.active, state.failed, state.progress],
  );

  // A Console line names a block: select it and bring it into view.
  const showBlock = (id: string) => {
    const node = nodes.find((n) => n.id === id);
    if (!node) return;
    // Open the group it's in, if that's collapsed.
    setNodes((ns) =>
      ns.map((n) =>
        n.id === node.parentId && isFrame(n) ? { ...n, data: { ...n.data, collapsed: false } } : { ...n, selected: n.id === id },
      ),
    );
    fitView({ nodes: [{ id }], duration: 300, maxZoom: 1.2 });
  };

  const view = state.stationView.view;

  return (
    <div className="app">
      <header>
        <b>Paperboat</b>
        <form
          onSubmit={(e) => {
            e.preventDefault();
            setUrl(draftUrl);
          }}
        >
          <input value={draftUrl} onChange={(e) => setDraftUrl(e.target.value)} size={24} />
          <button>Connect</button>
        </form>
        <span className={`conn conn-${state.connection}`}>
          {state.connection} {state.engine && `· ${state.engine}`}
        </span>
        <button disabled={state.connection !== "open"} onClick={() => send({ type: "graph.run", graph: toGraph(nodes, edges), ...(station ? { station } : {}), ...(Object.keys(runSettings).length ? { settings: runSettings } : {}) })}>
          Run
        </button>
        <RunOptions defaults={state.runDefaults} value={runSettings} onChange={setRunSettings} />
        <button disabled={!running} onClick={() => send({ type: "run.cancel" })}>
          Cancel
        </button>
        {state.run && (
          <span>
            <progress value={state.run.progress} /> {state.run.id} · {state.run.status}
          </span>
        )}
      </header>
      <div className={`station view-${view}`}>
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
          <StationGraph objects={state.objects} station={station ?? { objects: [] }} onChange={editStation} locked={running} />
        ) : (
          <SceneView key={JSON.stringify(state.view)} view={state.view} entities={state.entities} />
        )}
        {station && state.objects.length > 0 && (
          <StationPanel
            objects={state.objects}
            station={station}
            taken={new Set([...station.objects.map((o) => o.id), ...nodes.map((n) => n.id)])}
            onChange={editStation}
            locked={running}
          />
        )}
      </div>
      <RunStates.Provider value={runStates}>
        <FlowEditor
          blocks={state.blocks}
          nodes={nodes}
          edges={edges}
          setNodes={setNodes}
          setEdges={setEdges}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
        />
      </RunStates.Provider>
      <RunPanel
        state={state}
        onBlock={showBlock}
        monitors={Object.fromEntries(
          nodes.filter(isBlock).map((n) => [n.id, { kind: n.data.def.instrument ?? "graph", ...absolute(n, nodes) }]),
        )}
      />
    </div>
  );
}
