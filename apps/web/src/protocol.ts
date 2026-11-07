// Generated from protocol/schema/paperboat-0.schema.json by protocol/gen_ts.py.
// Do not edit: change the schema and run `npm run gen:protocol`.

export type VectorComponent = {
  /** The component's label, in vector order. */
  id: string;
  /** The component's unit (m, rad…). */
  unit?: string;
};

export type PortDef = {
  id: string;
  /** signal (a value, the default), step (control in a Step flow), a value's shape (image, points, vector…), or any (any value but a step). A wire joins ports of one type; vector and legacy numbers are compatible. */
  type: string;
  /** Its unit (N, m, rad, m/s…). A wire between two ports with units joins the same unit; a port without one takes any (#4). */
  unit?: string;
  /** A vector's fixed number of values. Absent: any length. */
  dimension?: number;
  /** A vector's ordered component labels and optional per-component units; its length fixes the dimension. */
  components?: VectorComponent[];
  /** A signal port of an elementwise block: it also joins a vector port (the block runs element by element). Absent: false. */
  broadcast?: boolean;
};

/** A setting on a block, or a property of a station object. number and boolean reach the block as numbers (boolean: a checkbox, 1 or 0); text, key (a KeyboardEvent.code, picked by pressing it) and file (a path, relative to the descriptor) as text; enum (one of options, a dropdown) as its name (and to an expression, its index). */
export type ParamDef = {
  id: string;
  type: "number";
  default: number;
  /** Its unit (kg, N/rad…), shown next to the field; a variable bound to it must be in the same unit. */
  unit?: string;
} | {
  id: string;
  type: "boolean";
  default: boolean;
} | {
  id: string;
  type: "text" | "key" | "file" | "variable";
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
  /** How the UI draws the block: a hint, so a UI without it (or a block without it) draws the normal card. */
  view?: {
    /** Draw a small plate: a glyph, bare port dots (names on hover), the name and main value in a caption below. */
    compact?: boolean;
    /** The plate's glyph: a curve name (gain, plus, step, clamp, const) or a few characters drawn as text (&, ≥1, ¬). */
    glyph?: string;
    /** The caption's value, an expression of the block's params: {k}, {min}…{max}. */
    value?: string;
  };
  /** A magic block's forms; its inputs and outputs are the first's, and its form param (an enum) picks one. */
  forms?: Form[];
  /** The id of a text param that decides the inputs: one per sign character in it (+ adds, - or − subtracts; at most 26; none reads as ++), ids a, b, c… of the first input's type. Sum's signs. A param called names gives one input per name instead (Mux), see outputs_from. */
  ports_from?: string;
  /** The id of a text param whose expression's free names are the inputs: one per identifier in it (other than pi, e and built-in functions, in order of first appearance), of the first input's type. f(t)'s f. */
  inputs_from?: string;
  /** The id of a text param of names (split on spaces and commas: "x v theta") that are the outputs, ids in that order; the first vector input gets one component per name, units by position from a `units` param. Demux's names. (ports_from "names" does the same for the inputs: Mux.) */
  outputs_from?: string;
  /** It works element by element: its signal ports take vectors too (they are marked broadcast), scalars broadcast, and with any vector in, its outputs are vectors of that length, one instance per element. Absent: false. */
  elementwise?: boolean;
  /** false: its outputs this step don't depend on its inputs this step (an integrator, a delay), so a feedback loop may pass through it. Absent: true. */
  feedthrough?: boolean;
  /** One line in plain words: what it does. Shown on its card in the block browser. Optional. */
  description?: string;
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
  /** One line in plain words: what it does. Shown on its card in the block browser. Optional. */
  description?: string;
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
  /** Show the floor grid (default true). The user can switch it on or off on the Station page, and that choice wins. */
  grid?: boolean;
  /** Where the camera opens, and where Home on the view cube returns it. Default: eye [2, 4, 8], looking at [0, 0, 0]. */
  camera?: {
    /** The camera's position. */
    eye: [number, number, number];
    /** The point it looks at and orbits (default [0, 0, 0]). */
    target?: [number, number, number];
    /** Vertical field of view in degrees (default 50). */
    fov?: number;
  };
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

/** One optional external toolkit from toolkits.toml and its status on this machine. */
export type ToolkitStatus = {
  name: string;
  repo: string;
  description?: string;
  /** Whether engine/toolkit/<name>/toolkit.toml is installed. */
  installed: boolean;
  /** Whether the current backend requires this toolkit. */
  required?: boolean;
  /** Currently installed release tag or 'dev'/'unknown'. */
  version?: string;
  /** Available release versions from the remote repository, newest first. */
  versions?: string[];
  /** Suggested default release version to install. */
  default?: string;
  /** Set while an operation is running for this toolkit. */
  busy?: "installing" | "updating" | "uninstalling";
};

export type GraphNode = {
  id: string;
  type: string;
  params?: Record<string, ParamValue>;
  /** Its place on the Flow canvas; backends ignore it. */
  pos?: [number, number];
  ports?: PortSides;
  /** Whether the block is commented out or bypassed. */
  commented?: boolean;
  /** Comment mode: out (omitted from execution) or through (bypassed). */
  comment_mode?: "out" | "through";
  /** User notes or documentation for this block. */
  comment?: string;
};

export type GraphEdge = {
  /** [node, port] */
  from: [string, string];
  /** [node, port] */
  to: [string, string];
};

/** How a Scope instrument draws (#37): presentation, kept in the Board layout (it changes during a run without a graph edit and saves with the project). Zoom, cursors and freeze are not saved. Every key is optional. */
export type ScopeSettings = {
  /** time: channels over time (default); xy: the first channel on x, the others on y; fft: dB magnitude of the visible window. */
  mode?: "time" | "xy" | "fft";
  /** Seconds of time shown while following the run. Absent: the whole run. */
  window?: number;
  /** A log y axis (time mode). */
  ylog?: boolean;
  /** Channels not drawn, by id (an input, or input.component for a vector). */
  hidden?: string[];
  /** line: straight lines between samples (default); line+points: also a ring on each sample; points: samples only; smooth: a monotone cubic that never overshoots. */
  draw?: "line" | "line+points" | "points" | "smooth";
  trigger?: {
    edge?: "rising" | "falling";
    /** The channel it watches. */
    source: string;
    level: number;
    /** Where the trigger point sits, % of the window. Absent: 10. */
    position?: number;
    /** Freeze on the first trigger (single); absent: keep re-triggering (auto). */
    single?: boolean;
  };
};

/** The Dashboard's grid: where each monitor block's instrument sits and how big it is, plus decorations (sections, separators, labels) and tabs. A backend ships one as its default (the descriptor's [dashboard]); an instrument not listed falls in where the automatic order puts it, and a block that does not exist is ignored. Decorations and tabs belong to the layout, not the flow: the Flow canvas still decides which instruments exist. */
export type DashboardLayout = {
  /** Grid columns; the UI fits fewer on a narrow screen. */
  columns?: number;
  instruments?: ({
    /** The monitor block's id. */
    block: string;
    /** compact: a readout card (1x1 cells); normal: a plot or picture to read at a glance (2x1); large: one to study (2x2); xlarge: one to fill the page with (3x3). Absent: the kind's default (display compact, graph normal, image large). */
    size?: "compact" | "normal" | "large" | "xlarge";
    /** Top-left cell: [column, row]. */
    at?: [number, number];
    /** The tab it is on (one of `tabs`); absent: the first. */
    tab?: string;
    scope?: ScopeSettings;
  })[];
  /** Pages of the Dashboard, each with its own grid. A tab strip shows when there are two or more. Absent: one page. */
  tabs?: string[];
  /** Things that are not instruments. section: a titled frame round a group of cells (takes no cells; `at` and `span` [w, h]). hline / vline: a separator on a grid line, in the gap, along `span` columns (hline, from the top of `at`'s row) or rows (vline, down the left of `at`'s column); takes no cells. label: text that takes cells like an instrument (`span` [w, h], default [2, 1]); a section's title and a label's text may hold `{block}` or `{block:.2f}`, which show that monitor's latest value (`{{` and `}}` are braces). console: the Console's lines, in a frame that takes cells (`span` [w, h], default [4, 3]); it has no text. station: a read-only view of the Station (the 3D scene), in a frame that takes cells (`span` [w, h], default [4, 3]); it has no text and no editing. */
  decorations?: ({
    kind: "section" | "hline" | "vline" | "label" | "console" | "station" | "controls";
    /** A section's title, or a label's text. `{name}` shows the monitor block's latest value (else the variable's value), `{block:.2f}` with that many decimals; `{{` and `}}` are literal braces. */
    text?: string;
    /** Top-left cell: [column, row]. */
    at: [number, number];
    /** A separator's length in cells (a whole number); a section's, label's or console's size [columns, rows]. */
    span?: number | [number, number];
    /** A label's look: heading (default) or note. */
    style?: "heading" | "note";
    /** The tab it is on (one of `tabs`); absent: the first. */
    tab?: string;
  })[];
};

/** A group of blocks. subsystem: a block you open to see its insides, like a Simulink subsystem; its ports are the wires crossing its edge. story: a subsystem that runs as one step of a Step flow (ports in -> next): its blocks once per clock step for params.seconds (0: once), fresh each time if params.reset. visual: a frame that tidies the canvas only. Groups nest (parent); the graph's wires stay flat, block to block, so backends treat a subsystem or a frame as if it weren't there. */
export type GraphGroup = {
  id: string;
  kind: "visual" | "subsystem" | "story";
  label?: string;
  /** Its blocks (not the groups in it: those name it as their parent). */
  nodes: string[];
  /** The group it is in (a subsystem, story or frame); absent: the top level. */
  parent?: string;
  collapsed?: boolean;
  /** Its top-left on the whole canvas; backends ignore it. */
  pos?: [number, number];
  params?: {
    /** How long the story runs when a path arrives (0: once). */
    seconds?: number;
    /** Its blocks start afresh each time a path arrives (a story in a loop starts over). */
    reset?: boolean;
  };
  /** Where a subsystem's or story's ports sit on its block (ids are node::port, or in and next). Backends ignore it. */
  ports?: PortSides;
  /** Ports a subsystem or story shows as a block even where no wire crosses its edge yet, in order. Backends ignore it. */
  expose?: ({
    side: "in" | "out";
    /** [node, port] inside the group: an input for side in, an output for side out. */
    to: [string, string];
  })[];
  /** Story outputs that read as unwired after its path leaves. */
  release?: [string, string][];
};

export type Vec3 = [number, number, number];

/** A param's value: a number, true / false (a boolean param), or text (text, key and file params). Or a reference (#36): {"var": name} takes a variable's value; {"expr": formula} (number and boolean params) a formula over variables. The backend resolves references before it makes blocks and objects. */
export type ParamValue = number | boolean | string | {
  /** A variable's name. */
  var: string;
} | {
  /** A formula over number and boolean variables: + - * / ^ %, comparisons, && || ?:, sqrt, sin, min, clamp… */
  expr: string;
};

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
  /** Run length, s (0: run indefinitely until cancelled or stopped; a Step flow: its limit). */
  duration?: number;
  /** Clock step, s. */
  dt?: number;
  /** Times real time (1: as the clock goes; 0: as fast as it can). */
  speed?: number;
};

/** A named, typed value (#36), used by reference in block params and station props. Exactly one of value and expr. */
export type Variable = {
  /** Letters, digits and _; not t, pi, e or a function's name. */
  name: string;
  type: "number" | "boolean" | "enum" | "text" | "file";
  /** Of its type: a number, true / false, or text (enum: one of options). */
  value?: number | boolean | string;
  /** A number or boolean variable's formula over other variables, instead of a value. */
  expr?: string;
  /** A number's unit (m, N, rad…). */
  unit?: string;
  min?: number;
  max?: number;
  /** How much one nudge in the UI changes it. */
  step?: number;
  /** A whole number. */
  whole?: boolean;
  /** An enum's choices. */
  options?: string[];
  /** One line: what it's for. */
  note?: string;
  /** Where it's listed (Cart-pole, Controller…). */
  group?: string;
  /** Also a control on the Dashboard (a Controls tile), for operators. */
  exposed?: boolean;
  /** Also a control in the run dock. */
  dock?: boolean;
  /** A number or boolean (not a formula) that can change during a run: the blocks bound to it take the new value live if they can. */
  tunable?: boolean;
};

/** The variables of a backend (its defaults) or the user's. */
export type Variables = Variable[];

/** Where a block's ports sit on the Flow canvas: the port ids on each side, in order. A port not listed keeps its place (inputs on the left, outputs on the right, after the listed ones). Backends ignore it. */
export type PortSides = {
  left?: string[];
  right?: string[];
  top?: string[];
  bottom?: string[];
};

/** A project file (#82): everything a user made, as one JSON file (name.ppj). It is not a protocol message; the UI writes and reads it. flow, station, variables and dashboard are the shapes the protocol already uses. A block whose type the opening backend lacks stays as a lost block, params and wires kept. */
export type Project = {
  /** The project format version. Opening refuses a newer one and says so. */
  paperboat: 0;
  /** The engine the project was made on (hello.engine). Opening it on another backend warns first. */
  backend: string;
  /** The Flow canvas, groups included; every node has its place (pos). */
  flow: Graph;
  station?: Station;
  /** The user's variables: the list the Variables sheet shows. */
  variables?: Variables;
  /** The user's Dashboard arrangement, if they made one. */
  dashboard?: DashboardLayout;
  /** Only the run settings the user changed from the backend's defaults. */
  run?: RunSettings;
};

/** Every message a backend sends. */
export type ServerMsg =
  | {
    type: "hello";
    protocol: "paperboat/0";
    engine: string;
    station?: StationView;
    run?: RunSettings;
    /** Available optional toolkits and their installed status on this machine. */
    toolkits?: ToolkitStatus[];
  }
  | {
    type: "catalog";
    blocks: BlockDef[];
    objects?: ObjectDef[];
    dashboard?: DashboardLayout;
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
    type: "variables.load";
    variables: Variables;
  }
  | {
    type: "variables.values";
    /** Each variable's resolved value, by name. */
    values: Record<string, number | boolean | string>;
    error?: string;
    at?: string;
  }
  | {
    type: "variables.patch";
    /** The variables that changed, by name, with their new values. */
    values: Record<string, number | boolean | string>;
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
    /** The latest scalar or text sample, or a whole numeric vector: on a Display, and on a Graph in a clock run (one trace per component). */
    v: number | string | number[];
    /** Labels and units for a vector sample, in vector order. */
    components?: VectorComponent[];
    /** The monitor's input this sample came in by, for one with several (a Scope's a, b…). Absent: in. */
    port?: string;
  }
  | {
    type: "node.state";
    node: string;
    state: NodeState;
  }
  | {
    type: "node.progress";
    node: string;
    fraction: number;
  }
  | {
    type: "log";
    level: "info" | "warn" | "error";
    text: string;
    node?: string;
  }
  | {
    type: "toolkits.status";
    toolkits: ToolkitStatus[];
  };

/** Every message the UI sends. */
export type ClientMsg =
  | {
    type: "graph.run";
    graph: Graph;
    station?: Station;
    settings?: RunSettings;
    /** The variables the graph's and station's references use; absent: the last ones set. */
    variables?: Variables;
  }
  | {
    type: "station.set";
    station: Station;
  }
  | {
    type: "variables.set";
    variables: Variables;
  }
  | {
    type: "run.cancel";
  }
  | {
    type: "input";
    key: string;
    down: boolean;
  }
  | {
    type: "backend.restart";
  }
  | {
    type: "toolkit.install";
    name: string;
    /** Release tag (e.g. 0.1.0) or 'dev' for main branch. Absent: latest release or dev. */
    version?: string;
  }
  | {
    type: "toolkit.versions";
    name: string;
  }
  | {
    type: "toolkit.update";
    name: string;
  }
  | {
    type: "toolkit.uninstall";
    name: string;
  };
