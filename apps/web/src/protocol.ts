// Wire types for protocol v0. The source of truth is protocol/schema/paperboat-0.schema.json;
// keep this in sync with it (and protocol/README.md).

/** `step` ports carry control in a Step flow; every other type carries values (`signal`, …). */
export type PortDef = { id: string; type: "signal" | "step" | (string & {}) };
/**
 * A setting on a block. `boolean` is a checkbox (true or false; 1 or 0 to the block),
 * `text` free text, `key` a KeyboardEvent.code ("Space", "ArrowDown") picked by pressing
 * it, `file` a path relative to the descriptor.
 */
export type ParamDef =
  | { id: string; type: "number"; default: number }
  | { id: string; type: "boolean"; default: boolean }
  | { id: string; type: "text" | "key" | "file"; default: string };
export type ParamValue = number | string | boolean;

export type BlockDef = {
  type: string;
  label: string;
  kind: "source" | "probe" | "function" | "monitor";
  inputs: PortDef[];
  outputs: PortDef[];
  params: ParamDef[];
  /** Palette group: the domain for its own blocks, else the toolkit package ("math", "step"). */
  group?: string;
  /** Spawned by a station object (same id): not in the palette, no params, removed with it. */
  linked?: boolean;
  /** A monitor's instrument on the Dashboard: "graph" (default), "display" (latest value), "image" (a picture URL). */
  instrument?: string;
};

/** A station object type: a thing on the Station page whose props are edited only there. */
export type ObjectDef = {
  type: string;
  label: string;
  group?: string;
  role: "body" | "joint";
  props: ParamDef[];
  /** Slots naming other objects (a joint's "parent", "child"). */
  links: string[];
  /** The linked block type it spawns on the Flow page, if any. */
  block?: string;
};

export type StationObject = {
  id: string;
  type: string;
  props: Record<string, ParamValue>;
  /** Link slot → the id of the station object filling it (a spring's "a" and "b"). */
  links?: Record<string, string>;
  /** Where it sits in the Station's graph view. */
  pos?: [number, number];
};
export type Station = { objects: StationObject[] };

export type Vec3 = [number, number, number];

export type Entity = {
  id: string;
  shape: "box" | "sphere" | "line" | "image";
  pos: Vec3;
  size?: Vec3;
  to?: Vec3;
  color?: string;
  /** An image entity's picture: a URL or a data: URL, drawn size[0] × size[1] facing +z. */
  src?: string;
  /** Rotation in radians, applied in x, y, z order (three.js "XYZ" Euler); boxes and spheres. */
  rot?: Vec3;
  /** Scale factors on top of `size`; boxes and spheres. */
  scale?: Vec3;
};

/** Camera hint sent with a scene. 2d: orthographic, looking down −z at `center`, `height` units tall. */
export type View = { mode: "3d" } | { mode: "2d"; center: [number, number]; height: number };

export type RunStatus = "done" | "failed" | "cancelled";
export type NodeState = "queued" | "running" | "cached" | "done" | "failed" | "cancelled";

export type GraphNode = { id: string; type: string; params: Record<string, ParamValue>; pos?: [number, number] };
/** How the Station page shows the station: the built-in scene, a graph of objects, or the backend's own page. */
export type StationView = { view: "scene" | "graph" | "web"; page?: string };
export type GraphEdge = { from: [string, string]; to: [string, string] };
/**
 * A group of blocks. `visual` tidies the canvas only; `story` runs as one step of a Step
 * flow (ports in → next): its blocks once per clock step for `params.seconds` (0: once).
 */
export type GraphGroup = {
  id: string;
  kind: "visual" | "story";
  label?: string;
  nodes: string[];
  collapsed?: boolean;
  pos?: [number, number];
  params?: { seconds?: number };
};
export type Graph = { nodes: GraphNode[]; edges: GraphEdge[]; groups?: GraphGroup[] };

export type ServerMsg =
  | { type: "hello"; protocol: string; engine: string; station?: StationView }
  | { type: "catalog"; blocks: BlockDef[]; objects?: ObjectDef[] }
  | { type: "scene"; entities: Entity[]; view?: View }
  | { type: "graph.load"; graph: Graph }
  | { type: "station.load"; station: Station }
  | { type: "scene.patch"; entities: (Partial<Entity> & { id: string })[] }
  | { type: "run.started"; run: string }
  | { type: "run.progress"; run: string; fraction: number }
  | { type: "signal"; node: string; t: number; v: number | string }
  | { type: "node.state"; node: string; state: NodeState }
  | { type: "log"; level: "info" | "warn" | "error"; text: string; node?: string }
  | { type: "run.finished"; run: string; status: RunStatus };

export type ClientMsg =
  | { type: "graph.run"; graph: Graph; station?: Station }
  | { type: "station.set"; station: Station }
  | { type: "run.cancel" }
  | { type: "input"; key: string; down: boolean };
