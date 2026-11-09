// The Flow canvas and the Station kept in step with the backend: what it loads them with, unless the
// user changed them since this same backend did; linked blocks that follow station objects; and
// wires that lost a block or a port.

import { useCallback, useEffect, useRef, useState, type Dispatch, type SetStateAction } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, Station } from "./protocol";
import { resolveBlocks, syncLinked, withoutLooseWires, type FlowNode, type Wire } from "./flowModel";
import { fromGraph, toGraph } from "./flowGraph";

export function useCanvasSync(
  state: EngineState,
  send: (m: ClientMsg) => void,
  nodes: FlowNode[],
  edges: Wire[],
  setNodes: Dispatch<SetStateAction<FlowNode[]>>,
  setEdges: Dispatch<SetStateAction<Wire[]>>,
  fitView: () => void,
) {
  const [station, setStation] = useState<Station | null>(null);
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
    setEdges((es) => withoutLooseWires(nodes, es));
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

  return { station, editStation };
}
