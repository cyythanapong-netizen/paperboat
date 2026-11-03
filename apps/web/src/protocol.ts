// Generated from protocol/schema/paperboat-0.schema.json by protocol/gen_ts.py.
// Do not edit: change the schema and run `npm run gen:protocol`.

export type PortDef = {
  id: string;
  /** signal (a value), step (control, in a Step flow) or a value shape (image, points, numbers...). A wire joins ports of one type. */
  type: string;
};

/** A setting on a block, or a property of a station object. number and boolean reach the block as numbers (boolean: a checkbox, 1 or 0); text, key (a KeyboardEvent.code, picked by pressing it) and file (a path, relative to the descriptor) as text; enum (one of options, a dropdown) as its name (and to an expression, its index). */
export type ParamDef = {
  id: string;
  type: "number";
  default: number;
} | {
  id: string;
  type: "boolean";
  default: boolean;
} | {
  id: string;
  type: "text" | "key" | "file";
  default: string;
} | {
  id: string;
  type: "enum";
  default: string;
  options: string[];
};

export type BlockDef = {
  type: string;
  label: string;
  /** source pushes into the domain, probe reads it, function computes, monitor shows a value on the Dashboard. */
  kind: "source" | "probe" | "function" | "monitor";
  inputs: PortDef[];
  outputs: PortDef[];
  params: ParamDef[];
  /** Palette group: the domain for its own blocks, else the toolkit package. */
  group?: string;
  /** Spawned by a station object (same id): not in the palette, no params, removed with it. */
  linked?: boolean;
  /** A monitor's instrument on the Dashboard: graph (default), display (the latest value) or image. */
  instrument?: string;
  /** A magic block's forms; its inputs and outputs are the first's, and its form param (an enum) picks one. */
  forms?: Form[];
};

/** A station object type: a thing on the Station page whose props are edited only there. */
export type ObjectDef = {
  type: string;
  label: string;
  role: "body" | "joint";
  group?: string;
  props: ParamDef[];
  /** Slots naming other station objects (a spring's a and b). */
  links: string[];
  /** The linked block type it spawns on the Flow page, if any. */
  block?: string;
};

/** One shape on the Station's scene. shape: box, sphere (size[0] = radius), line (pos to `to`), image (a size[0] x size[1] picture facing +z, from src). */
export type Entity = {
  id: string;
  shape: "box" | "sphere" | "line" | "image";
  pos: Vec3;
  size?: Vec3;
  /** Radians, applied in x, y, z order. */
  rot?: Vec3;
  /** Factors on top of size. */
  scale?: Vec3;
  to?: Vec3;
  color?: string;
  /** An image's picture: a URL or a data: URL. */
  src?: string;
};

/** Camera hint sent with a scene. 2d: orthographic, looking down -z at center, height units tall. */
export type View = {
  mode: "3d";
} | {
  mode: "2d";
  center: [number, number];
  height: number;
};

export type Graph = {
  nodes: GraphNode[];
  edges: GraphEdge[];
  groups?: GraphGroup[];
};

export type StationObject = {
  id: string;
  type: string;
  props?: Record<string, ParamValue>;
  /** Link slot to the id of the station object filling it. */
  links?: Record<string, string>;
  /** Where it sits in the Station's graph view. */
  pos?: [number, number];
};

export type Station = {
  objects: StationObject[];
};

export type GraphNode = {
  id: string;
  type: string;
  params?: Record<string, ParamValue>;
  /** Its place on the Flow canvas; backends ignore it. */
  pos?: [number, number];
};

export type GraphEdge = {
  /** [node, port] */
  from: [string, string];
  /** [node, port] */
  to: [string, string];
};

/** A group of blocks. visual: tidies the canvas only. story: runs as one step of a Step flow (ports in -> next): its blocks once per clock step for params.seconds (0: once), fresh each time if params.reset. */
export type GraphGroup = {
  id: string;
  kind: "visual" | "story";
  label?: string;
  nodes: string[];
  collapsed?: boolean;
  pos?: [number, number];
  params?: {
    /** How long the story runs when a path arrives (0: once). */
    seconds?: number;
    /** Its blocks start afresh each time a path arrives (a story in a loop starts over). */
    reset?: boolean;
  };
};

export type Vec3 = [number, number, number];

/** A param's value: a number, true / false (a boolean param), or text (text, key and file params). */
export type ParamValue = number | boolean | string;

export type RunStatus = "done" | "failed" | "cancelled";

/** What a run says about a block. A Step flow sends running (a path arrived) and done (it left); any run sends failed for the block at fault. */
export type NodeState = "running" | "done" | "failed";

/** How the Station page shows the station: the built-in scene, a graph of station objects, or the backend's own page (web; page is its path on the backend's port). */
export type StationView = {
  view: "scene" | "graph" | "web";
  page?: string;
};

/** A changed entity in scene.patch: its id and what changed. */
export type EntityPatch = {
  id: string;
  shape?: "box" | "sphere" | "line" | "image";
  pos?: Vec3;
  size?: Vec3;
  /** Radians, applied in x, y, z order. */
  rot?: Vec3;
  /** Factors on top of size. */
  scale?: Vec3;
  to?: Vec3;
  color?: string;
  /** An image's picture: a URL or a data: URL. */
  src?: string;
};

/** One form of a magic block: the ports it has in that form (signal: computes; step: branches on a Step flow's path). The block's form param picks one. */
export type Form = {
  name: string;
  inputs: PortDef[];
  outputs: PortDef[];
};

/** How a run goes. The backend's defaults come in hello.run; graph.run may override any of them except mode. */
export type RunSettings = {
  /** clock: every step for duration (and every Step flow); once: a Signal flow evaluated a single time. */
  mode?: "clock" | "once";
  /** Run length, s (a Step flow: its limit). */
  duration?: number;
  /** Clock step, s. */
  dt?: number;
  /** Times real time (1: as the clock goes; 0: as fast as it can). */
  speed?: number;
};

/** Every message a backend sends. */
export type ServerMsg =
  | {
    type: "hello";
    protocol: "paperboat/0";
    engine: string;
    station?: StationView;
    run?: RunSettings;
  }
  | {
    type: "catalog";
    blocks: BlockDef[];
    objects?: ObjectDef[];
  }
  | {
    type: "scene";
    entities: Entity[];
    view?: View;
  }
  | {
    type: "scene.patch";
    entities: EntityPatch[];
  }
  | {
    type: "graph.load";
    graph: Graph;
  }
  | {
    type: "station.load";
    station: Station;
  }
  | {
    type: "run.started";
    run: string;
  }
  | {
    type: "run.progress";
    run: string;
    fraction: number;
  }
  | {
    type: "run.finished";
    run: string;
    status: RunStatus;
  }
  | {
    type: "signal";
    node: string;
    t: number;
    v: number | string;
  }
  | {
    type: "node.state";
    node: string;
    state: NodeState;
  }
  | {
    type: "log";
    level: "info" | "warn" | "error";
    text: string;
    node?: string;
  };

/** Every message the UI sends. */
export type ClientMsg =
  | {
    type: "graph.run";
    graph: Graph;
    station?: Station;
    settings?: RunSettings;
  }
  | {
    type: "station.set";
    station: Station;
  }
  | {
    type: "run.cancel";
  }
  | {
    type: "input";
    key: string;
    down: boolean;
  };
