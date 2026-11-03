import { useCallback, useEffect, useReducer, useRef } from "react";
import type { BlockDef, ClientMsg, Entity, Graph, ObjectDef, RunStatus, ServerMsg, Station, StationView, View } from "./protocol";

export type Series = { t: number[]; v: number[] };

export type EngineState = {
  connection: "connecting" | "open" | "closed";
  engine: string;
  /** How the Station page shows the station (from hello). */
  stationView: StationView;
  blocks: BlockDef[];
  objects: ObjectDef[];
  entities: Record<string, Entity>;
  view: View;
  /** Last graph the backend asked the UI to load. */
  loaded: Graph | null;
  /** Last station the backend asked the UI to load. */
  station: Station | null;
  run: { id: string; progress: number; status: "running" | RunStatus } | null;
  signals: Record<string, Series>;
  /** The latest value each monitor sent: what a Display or Image view shows. */
  latest: Record<string, number | string>;
  /** Blocks whose last `node.state` was running (where a Step flow run is). */
  active: string[];
  /** Blocks the last run named as failing it. */
  failed: string[];
  /** Console lines; `node` is the block (or station object) that wrote it or failed. */
  logs: { level: string; text: string; node?: string }[];
};

const initial: EngineState = {
  connection: "connecting",
  engine: "",
  stationView: { view: "scene" },
  blocks: [],
  objects: [],
  entities: {},
  view: { mode: "3d" },
  loaded: null,
  station: null,
  run: null,
  signals: {},
  latest: {},
  active: [],
  failed: [],
  logs: [],
};

type Action = ServerMsg | { type: "reset" } | { type: "connection"; value: EngineState["connection"] };

function reduce(s: EngineState, m: Action): EngineState {
  switch (m.type) {
    case "reset":
      // A new URL may be a different backend: drop everything the old one sent.
      return initial;
    case "connection": {
      if (m.value !== "closed" || s.connection !== "open") return { ...s, connection: m.value };
      // Lost an open connection: a run in flight won't report back.
      const run = s.run?.status === "running" ? { ...s.run, status: "failed" as const } : s.run;
      const logs = [...s.logs, { level: "warn", text: "connection lost, reconnecting…" }];
      return { ...s, connection: "closed", run, logs };
    }
    case "hello":
      return { ...s, engine: m.engine, stationView: m.station ?? { view: "scene" } };
    case "catalog":
      return { ...s, blocks: m.blocks, objects: m.objects ?? [] };
    case "scene":
      return { ...s, entities: Object.fromEntries(m.entities.map((e) => [e.id, e])), view: m.view ?? s.view };
    case "graph.load":
      return { ...s, loaded: m.graph };
    case "station.load":
      return { ...s, station: m.station };
    case "scene.patch": {
      const entities = { ...s.entities };
      for (const p of m.entities) if (entities[p.id]) entities[p.id] = { ...entities[p.id], ...p };
      return { ...s, entities };
    }
    case "run.started":
      return { ...s, run: { id: m.run, progress: 0, status: "running" }, signals: {}, latest: {}, active: [], failed: [] };
    case "run.progress":
      return s.run?.id === m.run ? { ...s, run: { ...s.run, progress: m.fraction } } : s;
    case "run.finished":
      return s.run?.id === m.run ? { ...s, run: { ...s.run, status: m.status }, active: [] } : s;
    case "node.state": {
      const active = s.active.filter((id) => id !== m.node);
      const failed = m.state === "failed" ? [...s.failed.filter((id) => id !== m.node), m.node] : s.failed;
      return { ...s, active: m.state === "running" ? [...active, m.node] : active, failed };
    }
    case "signal": {
      const latest = { ...s.latest, [m.node]: m.v };
      if (typeof m.v !== "number") return { ...s, latest };
      const prev = s.signals[m.node] ?? { t: [], v: [] };
      return { ...s, latest, signals: { ...s.signals, [m.node]: { t: [...prev.t, m.t], v: [...prev.v, m.v] } } };
    }
    case "log":
      return { ...s, logs: [...s.logs.slice(-199), { level: m.level, text: m.text, node: m.node }] };
  }
}

const RETRY_MS = 1000;

/**
 * One WebSocket to a backend that speaks protocol v0. Reconnects when the backend
 * goes away; on reconnect the backend resends catalog, scene and starter graph.
 */
export function useEngine(url: string) {
  const [state, dispatch] = useReducer(reduce, initial);
  const socket = useRef<WebSocket | null>(null);
  // Who else hears every message as it arrives (a web station relays them to its page).
  const listeners = useRef(new Set<(m: ServerMsg) => void>());

  useEffect(() => {
    // Sockets from an earlier URL can still fire events after cleanup; `stopped` mutes them.
    let stopped = false;
    let retry: number | undefined;
    const connect = () => {
      const ws = new WebSocket(url);
      socket.current = ws;
      dispatch({ type: "connection", value: "connecting" });
      ws.onopen = () => !stopped && dispatch({ type: "connection", value: "open" });
      ws.onmessage = (e) => {
        if (stopped) return;
        const msg = JSON.parse(e.data) as ServerMsg;
        dispatch(msg);
        for (const listen of listeners.current) listen(msg);
      };
      ws.onclose = () => {
        if (stopped) return;
        dispatch({ type: "connection", value: "closed" });
        retry = window.setTimeout(connect, RETRY_MS);
      };
    };
    dispatch({ type: "reset" });
    connect();
    return () => {
      stopped = true;
      window.clearTimeout(retry);
      socket.current?.close();
    };
  }, [url]);

  const send = useCallback((m: ClientMsg) => {
    if (socket.current?.readyState === WebSocket.OPEN) socket.current.send(JSON.stringify(m));
  }, []);

  /** Hear every message from the backend; returns the unsubscribe. */
  const subscribe = useCallback((listen: (m: ServerMsg) => void) => {
    listeners.current.add(listen);
    return () => void listeners.current.delete(listen);
  }, []);

  return { state, send, subscribe };
}
