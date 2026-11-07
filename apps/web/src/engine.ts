import { useCallback, useEffect, useReducer, useRef } from "react";
import type { BlockDef, ClientMsg, DashboardLayout, Entity, Graph, ObjectDef, RunSettings, RunStatus, ServerMsg, Station, StationView, ToolkitStatus, Variables, VectorComponent, View } from "./protocol";

export type Series = { t: number[]; v: number[] };

export type EngineState = {
  connection: "connecting" | "open" | "closed";
  engine: string;
  /** How the Station page shows the station (from hello). */
  stationView: StationView;
  /** The backend's run defaults (from hello); empty if it didn't say. */
  runDefaults: RunSettings;
  /** Available optional toolkits and their installation status (from hello and toolkits.status). */
  toolkits: ToolkitStatus[];
  blocks: BlockDef[];
  objects: ObjectDef[];
  /** The backend's default Dashboard layout (from catalog), if it shipped one. */
  dashboard?: DashboardLayout;
  entities: Record<string, Entity>;
  view: View;
  /** Last graph the backend asked the UI to load. */
  loaded: Graph | null;
  /** Last station the backend asked the UI to load. */
  station: Station | null;
  /** The backend's default variables (#36), from variables.load. */
  variables: Variables;
  /** What the variables come to (variables.values), and why not when they don't hold. */
  varValues: Record<string, number | boolean | string>;
  varError: { text: string; at?: string } | null;
  run: { id: string; progress: number; status: "running" | RunStatus; startedAt?: number; simTime?: number } | null;
  signals: Record<string, Series>;
  /** A vector monitor's history in a clock run, one series per component (a Graph's traces). */
  traces: Record<string, Series[]>;
  /** The latest value each monitor sent: what a Display or Image view shows. */
  latest: Record<string, number | string | number[]>;
  /** Component labels for a vector a monitor shows (a Display's readout, a Graph's legend). */
  vectorComponents: Record<string, VectorComponent[]>;
  /** Blocks whose last `node.state` was running (where a Step flow run is). */
  active: string[];
  /** Blocks the last run named as failing it. */
  failed: string[];
  /** How far each block's long step has got (node.progress), 0 to 1. */
  progress: Record<string, number>;
  /** Console lines; `node` is the block (or station object) that wrote it or failed. */
  logs: { level: string; text: string; node?: string }[];
};

export const initial: EngineState = {
  connection: "connecting",
  engine: "",
  stationView: { view: "scene" },
  runDefaults: {},
  toolkits: [],
  blocks: [],
  objects: [],
  entities: {},
  view: { mode: "3d" },
  loaded: null,
  station: null,
  variables: [],
  varValues: {},
  varError: null,
  run: null,
  signals: {},
  traces: {},
  latest: {},
  vectorComponents: {},
  active: [],
  failed: [],
  progress: {},
  logs: [],
};

export type Action = ServerMsg | { type: "reset" } | { type: "connection"; value: EngineState["connection"] };

/** Samples kept per plotted signal: past this, the history is thinned, not cut. */
export const MAX_POINTS = 20000;

/** Max Console lines kept in memory. */
export const MAX_LOGS = 200;

export const DEFAULT_TOOLKITS: ToolkitStatus[] = [
  {
    name: "opencv",
    repo: "https://github.com/cyythanapong-netizen/paperboat-toolkit-opencv.git",
    description: "Image processing blocks, view monitor, and image station object (OpenCV).",
    installed: false,
  },
];

/** Known block types belonging to optional external toolkits, so missing blocks in a project or graph can identify their toolkit. */
const TOOLKIT_BLOCKS: Record<string, readonly string[]> = {
  opencv: [
    "gray",
    "blur",
    "median",
    "threshold",
    "adaptive_threshold",
    "hsv_range",
    "canny",
    "erode",
    "dilate",
    "open",
    "close",
    "invert",
    "distance",
    "view",
    "image",
  ],
};

/**
 * Return all toolkits that are required by the current backend or by missing blocks in the
 * open graph/project and are not currently installed.
 */
export function missingRequiredToolkits(
  toolkits: ToolkitStatus[],
  missingBlockTypes: Iterable<string> = [],
): ToolkitStatus[] {
  const list = toolkits.length > 0 ? toolkits : DEFAULT_TOOLKITS;
  const missingTypes = new Set(missingBlockTypes);
  return list.filter((t) => {
    if (t.installed) return false;
    if (t.required) return true;
    const known = TOOLKIT_BLOCKS[t.name];
    return !!known?.some((type) => missingTypes.has(type));
  });
}

function pushLog(logs: EngineState["logs"], entry: EngineState["logs"][number]): EngineState["logs"] {
  return [...logs.slice(-(MAX_LOGS - 1)), entry];
}

/**
 * A series with one more sample. A long run keeps its whole history: once a series has
 * MAX_POINTS samples, every other one is dropped, so the plot still spans the run at half
 * the resolution (memory and the cost of an update stay bounded).
 */
export function append(prev: Series | undefined, t: number, v: number): Series {
  const series = { t: [...(prev?.t ?? []), t], v: [...(prev?.v ?? []), v] };
  if (series.t.length <= MAX_POINTS) return series;
  const keep = (_: number, i: number) => i % 2 === 0 || i === series.t.length - 1; // always the latest
  return { t: series.t.filter(keep), v: series.v.filter(keep) };
}

export function reduce(s: EngineState, m: Action): EngineState {
  switch (m.type) {
    case "reset":
      // A new URL may be a different backend: drop everything the old one sent, keeping Console logs.
      return { ...initial, logs: s.logs };
    case "connection": {
      if (m.value !== "closed") return { ...s, connection: m.value };
      if (s.connection === "open") {
        // Lost an open connection: a run in flight won't report back.
        const run = s.run?.status === "running" ? { ...s.run, status: "failed" as const } : s.run;
        const logs = pushLog(s.logs, { level: "warn", text: "connection lost, reconnecting…" });
        return { ...s, connection: "closed", run, logs };
      }
      if (s.connection === "connecting" && s.logs.at(-1)?.text !== "waiting for backend, reconnecting…") {
        return { ...s, connection: "closed", logs: pushLog(s.logs, { level: "warn", text: "waiting for backend, reconnecting…" }) };
      }
      return { ...s, connection: "closed" };
    }
    case "hello": {
      const text = `connected to ${m.engine}`;
      const logs = s.logs.at(-1)?.text === text ? s.logs : pushLog(s.logs, { level: "info", text });
      return {
        ...s,
        engine: m.engine,
        stationView: m.station ?? { view: "scene" },
        runDefaults: m.run ?? {},
        toolkits: m.toolkits ?? s.toolkits,
        logs,
      };
    }
    case "toolkits.status":
      return { ...s, toolkits: m.toolkits };
    case "catalog":
      return { ...s, blocks: m.blocks, objects: m.objects ?? [], dashboard: m.dashboard };
    case "scene":
      return { ...s, entities: Object.fromEntries(m.entities.map((e) => [e.id, e])), view: m.view ?? s.view };
    case "graph.load":
      return { ...s, loaded: m.graph };
    case "station.load":
      return { ...s, station: m.station };
    case "variables.load":
      return { ...s, variables: m.variables };
    case "variables.values":
      return { ...s, varValues: m.values, varError: m.error ? { text: m.error, at: m.at } : null };
    case "scene.patch": {
      const entities = { ...s.entities };
      for (const p of m.entities) if (entities[p.id]) entities[p.id] = { ...entities[p.id], ...p };
      return { ...s, entities };
    }
    case "run.started":
      return { ...s, run: { id: m.run, progress: 0, status: "running", startedAt: Date.now(), simTime: 0 }, signals: {}, traces: {}, latest: {}, vectorComponents: {}, active: [], failed: [], progress: {} };
    case "run.progress":
      return s.run?.id === m.run ? { ...s, run: { ...s.run, progress: m.fraction } } : s;
    case "run.finished":
      return s.run?.id === m.run ? { ...s, run: { ...s.run, status: m.status }, active: [] } : s;
    case "node.state": {
      const active = s.active.filter((id) => id !== m.node);
      const failed = m.state === "failed" ? [...s.failed.filter((id) => id !== m.node), m.node] : s.failed;
      return { ...s, active: m.state === "running" ? [...active, m.node] : active, failed };
    }
    case "node.progress":
      return { ...s, progress: { ...s.progress, [m.node]: m.fraction } };
    case "signal": {
      const latest = { ...s.latest, [m.node]: m.v };
      const vectorComponents = m.components ? { ...s.vectorComponents, [m.node]: m.components } : s.vectorComponents;
      const run = s.run ? { ...s.run, simTime: Math.max(s.run.simTime ?? 0, m.t) } : s.run;
      if (Array.isArray(m.v)) {
        const v = m.v;
        const prev = s.traces[m.node] ?? [];
        return { ...s, run, latest, vectorComponents, traces: { ...s.traces, [m.node]: v.map((x, i) => append(prev[i], m.t, x)) } };
      }
      if (typeof m.v !== "number") return { ...s, run, latest, vectorComponents };
      return { ...s, run, latest, vectorComponents, signals: { ...s.signals, [m.node]: append(s.signals[m.node], m.t, m.v) } };
    }
    case "log":
      return { ...s, logs: pushLog(s.logs, { level: m.level, text: m.text, node: m.node }) };
    default: {
      const type = (m as { type?: unknown }).type;
      return { ...s, logs: pushLog(s.logs, { level: "warn", text: `ignored unknown message type "${String(type)}"` }) };
    }
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
        try {
          const msg = JSON.parse(e.data) as ServerMsg;
          dispatch(msg);
          for (const listen of listeners.current) listen(msg);
        } catch (err) {
          dispatch({ type: "log", level: "error", text: `bad message from backend: ${err instanceof Error ? err.message : String(err)}` });
        }
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

  /** Append a line to the Console from UI-side actions (even before the first run). */
  const log = useCallback((level: "info" | "warn" | "error", text: string, node?: string) => {
    dispatch({ type: "log", level, text, ...(node ? { node } : {}) });
  }, []);

  return { state, send, subscribe, log };
}
