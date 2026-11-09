import { useCallback, useEffect, useReducer, useRef } from "react";
import { TraceStore } from "./traces.ts";
import type { BlockDef, ClientMsg, Connectors, DashboardLayout, Entity, FlowType, Graph, MonitorState, ObjectDef, RunSettings, RunStatus, ServerMsg, Station, StationView, ToolkitStatus, Variables, VectorComponent, View } from "./protocol";

/** The run the backend is on, or last ran. */
export type RunState = {
  id: string;
  progress: number;
  status: "running" | RunStatus;
  startedAt?: number;
  simTime?: number;
  /** The speed the run took during it (run.speed, #219); absent: the one it started with. */
  speed?: number;
  /** Where the run was (wall clock, ms, and its own clock) when it took that speed: pacing goes on from there. */
  pacedFrom?: { at: number; simTime?: number; progress: number };
};

export type EngineState = {
  connection: "connecting" | "open" | "closed";
  engine: string;
  /** How the Station page shows the station (from hello). */
  stationView: StationView;
  /** The backend's run defaults (from hello); empty if it didn't say. */
  runDefaults: RunSettings;
  /** Available optional toolkits and their installation status (from hello and toolkits.status). */
  toolkits: ToolkitStatus[];
  /** The flow types a sheet may be (from hello, #9); absent: an older backend that doesn't say. */
  flows?: FlowType[];
  blocks: BlockDef[];
  /** The catalog's connector classes, for blocks' icon ports (§4). */
  connectors: Connectors;
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
  run: RunState | null;
  /** Every monitor's plotted history (traces.ts), kept outside React state. */
  traces: TraceStore;
  /** Ticks when `traces` changed, at most once per animation frame: what plots re-render on. */
  traceVersion: number;
  /** The latest value each monitor sent: what a Display or Image view shows. */
  latest: Record<string, number | string | number[]>;
  /** Component labels for a vector a monitor shows (a Display's readout, a Graph's legend). */
  vectorComponents: Record<string, VectorComponent[]>;
  /** Blocks whose last `node.state` was running (where a Step flow run is). */
  active: string[];
  /** Blocks the last run named as failing it. */
  failed: string[];
  /** Blocks waiting their turn in the run (node.state queued); none once it has finished. */
  queued: string[];
  /** Blocks the last run reused from an earlier one (node.state cached). */
  cached: string[];
  /** How far each block's long step has got (node.progress), 0 to 1. */
  progress: Record<string, number>;
  /** Each block's metric series (#17): points [x, y] in arrival order, at most MAX_METRIC_POINTS. */
  metrics: Record<string, Record<string, MetricSeries>>;
  /** What each monitor says (monitor.state), by its input (`in` when it has one): what a Lamp shows. */
  monitorStates: Record<string, Record<string, MonitorReading>>;
  /** The latest live picture each block drew (frame). */
  frames: Record<string, FrameMsg>;
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
  connectors: {},
  objects: [],
  entities: {},
  view: { mode: "3d" },
  loaded: null,
  station: null,
  variables: [],
  varValues: {},
  varError: null,
  run: null,
  traces: new TraceStore(),
  traceVersion: 0,
  latest: {},
  vectorComponents: {},
  active: [],
  failed: [],
  queued: [],
  cached: [],
  progress: {},
  metrics: {},
  monitorStates: {},
  frames: {},
  logs: [],
};

export type SignalMsg = Extract<ServerMsg, { type: "signal" }>;
export type FrameMsg = Extract<ServerMsg, { type: "frame" }>;
export type MetricSeries = { points: [number, number][]; unit?: string };
export type MonitorReading = { state: MonitorState; t: number; v?: number };

/** Max points kept per metric series (the oldest go first). */
export const MAX_METRIC_POINTS = 2000;
export type Action = ServerMsg | { type: "signals"; list: SignalMsg[] } | { type: "reset" } | { type: "connection"; value: EngineState["connection"] };

/** Max Console lines kept in memory. */
export const MAX_LOGS = 200;

/**
 * Return all toolkits that are required by the current backend or by missing blocks in the
 * open graph/project and are not currently installed.
 */
export function missingRequiredToolkits(
  toolkits: ToolkitStatus[],
  missingBlockTypes: Iterable<string> = [],
): ToolkitStatus[] {
  const missingTypes = new Set(missingBlockTypes);
  return toolkits.filter((t) => !t.installed && (t.required || !!t.blocks?.some((type) => missingTypes.has(type))));
}

/** The word for what the last run says of a block, if anything: failed, running, queued, cached (done says nothing). */
export function runWord(
  states: { active: ReadonlySet<string>; failed: ReadonlySet<string>; queued: ReadonlySet<string>; cached: ReadonlySet<string> },
  id: string,
): "failed" | "running" | "queued" | "cached" | undefined {
  if (states.failed.has(id)) return "failed";
  if (states.active.has(id)) return "running";
  if (states.queued.has(id)) return "queued";
  if (states.cached.has(id)) return "cached";
  return undefined;
}

function pushLog(logs: EngineState["logs"], entry: EngineState["logs"][number]): EngineState["logs"] {
  return [...logs.slice(-(MAX_LOGS - 1)), entry];
}

export function reduce(s: EngineState, m: Action): EngineState {
  switch (m.type) {
    case "reset":
      // A new URL may be a different backend: drop everything the old one sent, keeping Console logs.
      return { ...initial, logs: s.logs, traces: s.traces, traceVersion: s.traceVersion + 1 };
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
        flows: m.flows,
        logs,
      };
    }
    case "toolkits.status":
      return { ...s, toolkits: m.toolkits };
    case "catalog":
      return { ...s, blocks: m.blocks, connectors: m.connectors ?? {}, objects: m.objects ?? [], dashboard: m.dashboard };
    case "scene":
      return { ...s, entities: Object.fromEntries(m.entities.map((e) => [e.id, e])), view: m.view ?? s.view };
    case "graph.load":
      return { ...s, loaded: m.graph };
    case "station.load":
      return { ...s, station: m.station };
    case "variables.load":
      return { ...s, variables: m.variables };
    case "project.load":
      return s;
    case "variables.values":
      return { ...s, varValues: m.values, varError: m.error ? { text: m.error, at: m.at } : null };
    case "variables.patch":
      return { ...s, varValues: { ...s.varValues, ...m.values } };
    case "scene.patch": {
      const entities = { ...s.entities };
      for (const p of m.entities) if (entities[p.id]) entities[p.id] = { ...entities[p.id], ...p };
      return { ...s, entities };
    }
    case "run.started":
      return {
        ...s,
        run: { id: m.run, progress: 0, status: "running", startedAt: Date.now(), simTime: 0 },
        traceVersion: s.traceVersion + 1,
        latest: {},
        vectorComponents: {},
        active: [],
        failed: [],
        queued: [],
        cached: [],
        progress: {},
        metrics: {},
        monitorStates: {},
        frames: {},
      };
    case "run.progress":
      return s.run?.id === m.run ? { ...s, run: { ...s.run, progress: m.fraction } } : s;
    case "run.finished":
      // What never got its turn (the run failed or was cancelled first) is no longer waiting.
      return s.run?.id === m.run ? { ...s, run: { ...s.run, status: m.status }, active: [], queued: [] } : s;
    case "run.speed":
      // The run took a new speed (#219): from here on it paces at it.
      if (!s.run || s.run.status !== "running" || (m.run !== undefined && m.run !== s.run.id)) return s;
      return { ...s, run: { ...s.run, speed: m.speed, pacedFrom: { at: Date.now(), simTime: s.run.simTime, progress: s.run.progress } } };
    case "node.state": {
      // A block is in one of active, queued and cached at a time (done: none); failed stays.
      const without = (list: string[]) => list.filter((id) => id !== m.node);
      const add = (list: string[], state: string) => (m.state === state ? [...without(list), m.node] : without(list));
      const failed = m.state === "failed" ? add(s.failed, "failed") : s.failed;
      return { ...s, active: add(s.active, "running"), queued: add(s.queued, "queued"), cached: add(s.cached, "cached"), failed };
    }
    case "node.progress":
      return { ...s, progress: { ...s.progress, [m.node]: m.fraction } };
    case "metric": {
      const series = s.metrics[m.node] ?? {};
      const before = series[m.series]?.points ?? [];
      const points: [number, number][] = [...before.slice(-(MAX_METRIC_POINTS - 1)), [m.x, m.y]];
      const one: MetricSeries = m.unit ? { points, unit: m.unit } : { points };
      return { ...s, metrics: { ...s.metrics, [m.node]: { ...series, [m.series]: one } } };
    }
    case "monitor.state": {
      const reading: MonitorReading = m.v === undefined ? { state: m.state, t: m.t } : { state: m.state, t: m.t, v: m.v };
      const ports = { ...s.monitorStates[m.node], [m.port ?? "in"]: reading };
      return { ...s, monitorStates: { ...s.monitorStates, [m.node]: ports } };
    }
    case "frame":
      return { ...s, frames: { ...s.frames, [m.node]: m } };
    case "signal": {
      // Its history is in `traces` already (useEngine adds it as it arrives); here only the
      // latest value, for readouts.
      const latest = { ...s.latest, [m.node]: m.v };
      const vectorComponents = m.components ? { ...s.vectorComponents, [m.node]: m.components } : s.vectorComponents;
      const run = s.run ? { ...s.run, simTime: Math.max(s.run.simTime ?? 0, m.t) } : s.run;
      return { ...s, run, latest, vectorComponents };
    }
    case "signals": {
      let out = s;
      for (const one of m.list) out = reduce(out, one);
      return { ...out, traceVersion: s.traceVersion + 1 };
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
  // Each engine has its own trace store; the reducer only carries it along.
  const [state, dispatch] = useReducer(reduce, initial, (i) => ({ ...i, traces: new TraceStore() }));
  const traces = state.traces;
  const socket = useRef<WebSocket | null>(null);
  // Who else hears every message as it arrives (a web station relays them to its page).
  const listeners = useRef(new Set<(m: ServerMsg) => void>());

  useEffect(() => {
    // Sockets from an earlier URL can still fire events after cleanup; `stopped` mutes them.
    let stopped = false;
    let retry: number | undefined;
    let pending: SignalMsg[] = [];
    let frame: number | undefined;
    const flush = () => {
      if (frame !== undefined) cancelAnimationFrame(frame);
      frame = undefined;
      if (!pending.length) return;
      const list = pending;
      pending = [];
      dispatch({ type: "signals", list });
    };
    const connect = () => {
      const ws = new WebSocket(url);
      socket.current = ws;
      dispatch({ type: "connection", value: "connecting" });
      ws.onopen = () => !stopped && dispatch({ type: "connection", value: "open" });
      ws.onmessage = (e) => {
        if (stopped) return;
        try {
          const msg = JSON.parse(e.data) as ServerMsg;
          if (msg.type === "signal") {
            // Plots read the store; React hears of a frame's samples once (#37).
            traces.add(msg);
            pending.push(msg);
            frame ??= requestAnimationFrame(flush);
          } else {
            flush();
            if (msg.type === "run.started") traces.clear();
            dispatch(msg);
          }
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
      if (frame !== undefined) cancelAnimationFrame(frame);
      window.clearTimeout(retry);
      const ws = socket.current;
      if (ws?.readyState === WebSocket.CONNECTING) {
        // Closing a connecting socket logs a browser warning; close it once it is open.
        ws.addEventListener("open", () => ws.close(), { once: true });
      } else if (ws?.readyState === WebSocket.OPEN) {
        ws.close();
      }
    };
  }, [url, traces]);

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
