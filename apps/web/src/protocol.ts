// Generated from protocol/schema/paperboat-0.schema.json by protocol/gen_ts.py.
// Do not edit: change the schema and run `npm run gen:protocol`.

export type PortDef = {
  id: string;
  /** signal (a value, the default), step (control in a Step flow), a value's shape (image, points, numbers…), or any (any value but a step). A wire joins ports of one type; any joins anything but a step (#4). */
  type: string;
  /** Its unit (N, m, rad, m/s…). A wire between two ports with units joins the same unit; a port without one takes any (#4). */
  unit?: string;
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
  /** The id of a text param that decides the inputs: one per sign character in it (+ adds, - or − subtracts; at most 26; none reads as ++), ids a, b, c… of the first input's type. Sum's signs. */
  ports_from?: string;
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
  })[];
  /** Pages of the Dashboard, each with its own grid. A tab strip shows when there are two or more. Absent: one page. */
  tabs?: string[];
  /** Things that are not instruments. section: a titled frame round a group of cells (takes no cells; `at` and `span` [w, h]). hline / vline: a separator on a grid line, in the gap, along `span` columns (hline, from the top of `at`'s row) or rows (vline, down the left of `at`'s column); takes no cells. label: text that takes cells like an instrument (`span` [w, h], default [2, 1]); a section's title and a label's text may hold `{block}` or `{block:.2f}`, which show that monitor's latest value (`{{` and `}}` are braces). console: the Console's lines, in a frame that takes cells (`span` [w, h], default [4, 3]); it has no text. */
  decorations?: ({
    kind: "section" | "hline" | "vline" | "label" | "console";
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
  /** Run length, s (a Step flow: its limit). */
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
  /** Where it's listed (Plant, Controller…). */
  group?: string;
  /** Also a control on the Dashboard, for operators. */
  exposed?: boolean;
};

/** The variables of a backend (its defaults) or the user's. */
export type Variables = Variable[];

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
    type: "node.progress";
    node: string;
    fraction: number;
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
  };
