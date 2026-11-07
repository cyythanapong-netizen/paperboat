# Paperboat protocol v0

One WebSocket between the UI and a backend (the C++ engine, or any example backend).
Every message is a JSON object with a `type` field. The UI holds no domain knowledge:
the palette, node forms and 3D view are built only from what the backend sends. Message
shapes are defined by the JSON Schema in [schema/paperboat-0.schema.json](schema/paperboat-0.schema.json)
(`$defs/toUI`, `$defs/toBackend`); this page explains them.

Default address: `ws://localhost:8765`.

## Backend → UI

| `type`         | Fields                                      | Sent                         |
| -------------- | ------------------------------------------- | ---------------------------- |
| `hello`        | `protocol: "paperboat/0"`, `engine: string`, `station?: { view: "scene" \| "graph" \| "web", page?: string }`, `run?: RunSettings` (its defaults), `toolkits?: ToolkitStatus[]` | on connect. `station.page` (web view only) is a path on the backend's own port, over HTTP |
| `catalog`      | `blocks: BlockDef[]`, `connectors?: Connectors` (see below), `objects?`, `dashboard?` | on connect                   |
| `scene`        | `entities: Entity[]`, `view?: View`         | on connect, and on reset     |
| `graph.load`   | `graph: { nodes, edges }`                   | on connect (starter graph)   |
| `station.load` | `station: Station`                          | on connect, after `graph.load`, if the catalog has `objects` |
| `variables.load` | `variables: Variable[]`                   | on connect, before `graph.load`, if the backend has default variables (its descriptor's `[[variables]]`) |
| `variables.values` | `values: { name: value }`, `error?: string`, `at?: string` | on connect and after each `variables.set`: what every variable comes to (a formula's result), or why they don't hold (`at`: the variable or block at fault) |
| `variables.patch` | `values: { name: value }` | during a run, when a block set tunable variables (`Context::set_variable`, #166): what changed, formulas that follow included. The UI keeps the non-formula ones in the project; the backend already keeps them for the next run |
| `scene.patch`  | `entities: ({id} & Partial<Entity>)[]`      | during a run                 |
| `run.started`  | `run: string`                               | when a run starts            |
| `run.progress` | `run: string`, `fraction: number` (0–1)     | during a run                 |
| `signal`       | `node: string`, `t: number`, `v: number \| string \| number[]`, `components?: VectorComponent[]`, `port?: string` | per sample, for monitor nodes. A Scope (several inputs) sends one per wired input, `port` naming it (absent: `in`), and keeps a vector whole. `t` is the x value: time in a simulation, list index otherwise; a Display monitor, and a Graph in a clock run (one trace per component), get a whole vector as `number[]` with its source component labels; a Graph in a run-once gets it by index. A layout instrument's optional `scope` holds a Scope's settings (`mode`, `window`, `ylog`, `hidden`, `draw`, `trigger`; schema `ScopeSettings`) |
| `node.state`   | `node: string`, `state: "running" \| "done" \| "failed"` (later also `queued`, `cached`, `cancelled`) | A Step flow sends `running` when a path arrives at a block and `done` when it leaves; a run-once sends each block `running` then `done` as it evaluates them; any run sends `failed` for the block that failed it (an unknown type, an error), which the UI marks |
| `node.progress` | `node: string`, `fraction: number` (0–1) | How far a block's long step has got (`Context::progress`), at most ~20 a second per block; the UI shows it on the running block |
| `log`          | `level: "info" \| "warn" \| "error"`, `text`, `node?: string` | any time. `node`: the block or station object that wrote it, or that failed |
| `toolkits.status` | `toolkits: ToolkitStatus[]`              | when an optional toolkit's installation or busy state changes |
| `run.finished` | `run: string`, `status: "done" \| "failed" \| "cancelled"` | when a run ends |

## UI → Backend

| `type`       | Fields                     |
| ------------ | -------------------------- |
| `graph.run`  | `graph: { nodes, edges }`, `station?: Station` (the backend's current one if absent), `settings?: RunSettings` (overrides the backend's defaults for this run), `variables?: Variable[]` (the backend's current ones if absent) |
| `station.set` | `station: Station`: an edit on the Station page; the backend checks it and answers with a `scene` (or a `log` error) |
| `variables.set` | `variables: Variable[]`: the user's variables changed; the backend checks them, keeps them for the next run and answers with a `scene` (the station they shape), or a `log` error. During a run it answers with `variables.values` only, and the run takes the `tunable` ones (below) |
| `run.cancel` | —                          |
| `input`      | `key: string` (a `KeyboardEvent.code`, e.g. `"Space"`), `down: boolean` |
| `backend.restart` | — (restarts the backend process with its startup arguments) |
| `toolkit.install` | `name: string`, `version?: string` (installs the named toolkit from `toolkits.toml` and rebuilds the engine; version is tag like `0.1.0` or `dev`) |
| `toolkit.versions` | `name: string` (requests available release tags for the named toolkit) |
| `toolkit.update`  | `name: string` (updates the named toolkit from its repository and rebuilds the engine) |
| `toolkit.uninstall` | `name: string` (removes the named toolkit and rebuilds the engine) |

## Shapes

```jsonc
// BlockDef: one entry in the flow palette
{
  "type": "sine",                  // unique id of the block type
  "label": "Sine",
  "description": "A smooth wave: amplitude, frequency, phase.",  // optional: one line, shown on its card
  "kind": "function",              // source | probe | function | monitor
  "inputs":  [{ "id": "u", "type": "signal" }],
  "outputs": [{ "id": "y", "type": "signal" }],
  "params":  [{ "id": "amplitude", "type": "number", "default": 5 }],
  // other param types: { "type": "boolean", "default": true } (a checkbox; 1 or 0 to the block),
  // { "type": "text" | "key" | "file" | "variable", "default": "…" },
  // { "type": "enum", "options": ["in-out", "in"], "default": "in-out" } (a choice; the block gets its index)
  "group": "base",                 // optional palette group: the domain, or a toolkit package
  // optional, how to draw it (a hint: absent, or a UI that ignores it, draws the normal card).
  // compact: a small plate with a glyph, bare port dots (names on hover) and a caption with the
  // block's name and value. glyph: a curve (gain, plus, step, clamp, const) or a few characters
  // drawn as text; value: an expression of the params. Every key is optional.
  "view": { "compact": true, "glyph": "gain", "value": "{k}" },
  // view.icon (optional, additive): a custom look, drawn instead of the glyph; see "Custom block look"
  "feedthrough": false,            // optional: its outputs don't depend on this step's inputs
                                   // (an integrator, a delay), so a feedback loop may pass through it
  // optional, a magic block: one form per way to use it, each with its own ports. Its enum
  // param "form" picks one (the first by default); inputs/outputs are the first form's.
  // A node takes the ports of its form, and the engine makes the block that form needs.
  // optional: the id of a text param that decides the inputs, one per sign character in it
  // ("+-+": a, b, c; + adds, - or − subtracts; at most 26, none reads as "++"), of the first
  // input's type. Sum's "signs". The UI and the engine both take the ports from the param.
  "ports_from": "signs",
  // optional: the id of a text param whose expression's free names (identifiers other than
  // pi, e, and built-in functions, in order of first appearance) become the block's inputs of
  // the first input's type. f(t)'s "f". Deleting a name removes its port and drops any wire to
  // it; renaming a name (x → dx) drops the wire to the old name rather than moving it.
  "inputs_from": "f",
  // optional: the id of a text param of names ("x v theta", space- or comma-separated) that
  // become the outputs, in order; the first vector input gets one component per name, and a
  // "units" param ("m m/s rad") gives each a unit by position. Demux's "names". With
  // ports_from "names" the same goes for the inputs (Mux). Bad names fail the run on the block.
  "outputs_from": "names",
  // optional: the block works element by element (Gain, Sum, Integrator, PID…). Its signal
  // ports carry "broadcast": true and also join a vector port. With a vector in, the backend
  // runs it once per element and its outputs are vectors of that length; with numbers, as
  // before. Lengths are the backend's to check; the UI never refuses a wire for one it can't know.
  "elementwise": true,
  "forms": [
    { "name": "signal", "inputs": [{ "id": "cond" }, { "id": "a" }, { "id": "b" }], "outputs": [{ "id": "y" }] },
    { "name": "step", "inputs": [{ "id": "in", "type": "step" }, { "id": "cond" }],
      "outputs": [{ "id": "yes", "type": "step" }, { "id": "no", "type": "step" }] }
  ]
}

// Entity: one thing in the 3D setup view
{ "id": "mass", "shape": "box", "pos": [0, 0, 0], "size": [1, 1, 1], "color": "#2f5d62" }
{ "id": "spring", "shape": "line", "pos": [-2.9, 0, 0], "to": [-0.5, 0, 0] }
{ "id": "body", "shape": "box", "pos": [0, 0.25, 0], "size": [1, 0.5, 0.6], "rot": [0, 1.57, 0], "scale": [2, 2, 2] }
// shape: box | sphere (size[0] = radius) | line (pos → to)
// rot: optional, radians, applied in x, y, z order; scale: optional factors on top of size

// View: camera hint sent with a scene (default 3d)
{ "mode": "3d" }                                   // "grid": false hides the floor grid (default true; the user's own switch wins)
{ "mode": "3d", "camera": { "eye": [3, 2, 6], "target": [0, 0.5, 0], "fov": 40 } }
                                                   // where the camera opens and Home returns (default eye [2, 4, 8] at [0, 0, 0], fov 50)
{ "mode": "2d", "center": [6, 5], "height": 11 }   // orthographic, looking down −z

// RunSettings: hello.run (the backend's defaults) and graph.run.settings (this run's
// changes; any but mode). A value out of range fails the run with a log, not the backend.
// duration: 0 means run indefinitely until cancelled, a Stop block, or every path ending.
{ "mode": "clock", "duration": 10, "dt": 0.01, "speed": 1 }   // mode: clock | once; speed: times real time, 0 as fast as it can

// Graph: sent by the UI with graph.run, and by the backend with graph.load
{
  "nodes": [{ "id": "sine-1", "type": "sine", "params": { "amplitude": 5, "frequency": 0.3 }, "pos": [0, 0] }],
  "edges": [{ "from": ["sine-1", "y"], "to": ["F1-1", "force"] }]
}
```

`pos` is the node's place on the flow canvas; it is optional and backends ignore it.
Missing `params` take their defaults. A param's value is a number, or for a `key` param a
`KeyboardEvent.code` string (`"ArrowDown"`), which the UI sets by the user pressing it.
A param (or a station object's prop) may instead **refer to a variable**:
`{ "var": "pole_length" }`, or for a number or boolean param hold a formula over
variables: `{ "expr": "2 * sqrt(kp_theta * pole_length)" }`. The backend resolves
references before it makes blocks and objects (a block only ever sees numbers and text)
and fails the run on the block at fault: an unknown variable (with the nearest name), a
type that doesn't fit, an enum value not among the param's options, a bad formula. Port `type` is `signal` (a value), `step` (control,
in a Step flow), a value shape (`image`, `points`, `vector`, …) or `any` (any value but a
step). `vector` carries an ordered JSON array of numbers on one wire; legacy `numbers` ports
remain compatible (a `broadcast` signal port, on an elementwise block, also joins one). A vector port may declare a fixed `dimension` and ordered `components`
(`{id, unit?}`); without them its length is dynamic and component labels/units are unknown.
A port, and a number param, may have a `unit` (`"N"`, `"m"`, `"rad"`). On a vector, `unit`
applies to every component unless that component has its own unit. A wire joins
ports of one type (`any` joins anything but a step), in one unit per component when both
ends declare one, and with matching dimensions when both are fixed: the UI enforces it while
dragging, and a backend fails a run whose wires break it. A signal
input takes one wire; a step output leads to one block. A run may step a clock (simulation: `scene.patch` per frame) or evaluate
once (parametric: one `scene` at the end); the UI handles both.

## Custom block look: `view.icon` and `connectors`

A block can draw itself instead of a glyph, and put its ports where its picture has them
(Modelica icons, circuit symbols). Both are additive: a UI or a block without them draws the
plain plate. The shapes are `IconDef`, `Prim`, `IconPort` and `ConnectorDef` in the schema.

```jsonc
"view": { "icon": {
  "w": 2, "h": 1,                       // plate cells: 1x1 (64x56), 2x1 (128x56) or 1x2 (64x112); 2x2 is invalid
  "g": [                                // primitives in a 0..100 box, coordinates multiples of 5
    { "t": "line", "p": [[0, 50], [20, 50]], "stroke": "ink", "weight": "normal" },
    { "t": "poly", "p": [[20, 30], [80, 30], [50, 70]], "fill": "raised" },
    { "t": "rect", "x": 20, "y": 30, "w": 60, "h": 40, "corner": "md", "fill": "none" },
    { "t": "ellipse", "x": 25, "y": 25, "w": 50, "h": 50, "stroke": "muted" },
    { "t": "arc", "x": 10, "y": 10, "w": 80, "h": 80, "a0": 0, "a1": 180 },
    { "t": "text", "x": 50, "y": 85, "s": "{R}", "font": "mono", "size": 12 }   // {param} is filled like a caption
  ],
  "ports": [                            // handles on the plate's edge
    { "id": "p", "class": "PositivePin", "side": "left", "at": 50, "variant": "filled" },
    { "id": "n", "class": "NegativePin", "side": "right", "at": 50 }
  ]
} }
```

### Primitives (`Prim`)

Each primitive in `g` is positioned on a 0..100 grid (coordinates in multiples of 5, inset 8px on the plate)
and accepts optional styling attributes:

- `line`: `{ "t": "line", "p": [[0, 50], [20, 50]] }` — a multi-segment line along point list `p`.
- `poly`: `{ "t": "poly", "p": [[20, 20], [80, 20], [50, 80]] }` — a closed polygon along point list `p`.
- `rect`: `{ "t": "rect", "x": 20, "y": 20, "w": 60, "h": 60, "corner": "md" }` — a rectangle with optional corner radius.
- `ellipse`: `{ "t": "ellipse", "x": 10, "y": 20, "w": 80, "h": 60 }` — an ellipse bounding box.
- `arc`: `{ "t": "arc", "x": 10, "y": 10, "w": 80, "h": 80, "a0": 0, "a1": 180 }` — an elliptical arc from angle `a0` to `a1` (degrees).
- `text`: `{ "t": "text", "x": 50, "y": 50, "s": "M", "font": "sans", "size": 12 }` — text label; `s` may interpolate block params via `{param}`.

### Token Table

All primitive visual styles must be token names; raw colors, hex codes, or arbitrary SVG path strings are strictly rejected:

| Property | Values | Description |
| --- | --- | --- |
| `stroke` | `"ink"` (default), `"muted"`, `"signal"` | `--text`, `--line-strong`, `--signal` (live state only, never decoration) |
| `weight` | `"thin"`, `"normal"` (default), `"wire"` | 1px, 1.5px, 2px |
| `dash` | `"solid"` (default), `"axis"` | Solid or `2 2` axis dash (never red; dashed red is reserved for missing) |
| `fill` | `"none"` (default), `"ink"`, `"raised"`, `"sunken"` | `--text`, `--surface-raised`, `--surface-sunken` |
| `corner` | `"0"` (default), `"sm"`, `"md"` | 0px, 3px, 5px radius |
| `font` | `"sans"` (default), `"mono"` | `--font-sans`, `--font-mono` |
| `size` | `12` (default), `12.5`, `13`, `15`, `30` | Text font size in pixels |

Defaults: stroke `ink`, weight `normal`, dash `solid`, fill `none`, corner `0`, font `sans`, size `12`. Stroke caps and joins are always round.

**Fallback rule:** An unknown primitive kind, unrecognized token or size, a 2x2 plate (valid sizes are 1x1, 2x1, 1x2), or an off-grid coordinate causes the UI to drop the entire icon, fall back to drawing the plain plate with a glyph, and log a `console.warn` once per block type. Live execution state (selected, running, failed) is displayed exclusively by the plate container, never by the icon graphic.

### Ports (`IconPort`)

Handles are placed on the plate boundary:
- `id`: unique port identifier.
- `class`: key matching an entry in `catalog.connectors`.
- `side`: `"left"` | `"right"` | `"top"` | `"bottom"`.
- `at`: 0..100 along the designated edge, in multiples of 5.
- `variant`: `"hollow"` | `"filled"` (identifies a/b or positive/negative side, never live state).
- `array`: parameter ID holding count `n` (#164 vector port; draws a single port handle with a count badge and fan-out behavior).
- `if`: parameter ID controlling presence; when falsy, the port is hidden, remaining ports on that side re-spread evenly, and any connected wires render as dashed red (`missing`).

### Connectors (`ConnectorDef`)

Top-level `connectors` in `catalog` defines the physical/causal domain connector classes:
- `kind`: `"acausal"` | `"input"` | `"output"` | `"expandable"` (reserved).
- `handle`: `"square"` | `"diamond"` | `"ring2"` | `"triangle"` | `"circle"`.
- `alt`: optional swap look for `"filled"` / `"hollow"` variants.
- `size`: handle dimension (8, 10, or 12 px; hit area is always 40px `wireSnap` radius).
- `weight`: wire line thickness (1.2, 1.8, or 2.0 px).
- `dash`: optional wire dash (`"solid"` | `"dashed"` for thermal grey wires).
- `potential`, `flow`, `stream`: optional quantities list (`[{ id, unit }]`).

Built-in class table:

| Class | Kind | Handle | Size | Wire | Variant |
| --- | --- | --- | --- | --- | --- |
| `Pin`, `PositivePin`, `NegativePin` | acausal | square | 8 | 1.2 | p filled, n hollow |
| `Flange_a`, `Flange_b` | acausal | square | 10 | 1.8 | a filled, b hollow |
| `HeatPort_a`, `HeatPort_b` | acausal | diamond | 8 | 1.2 dashed | |
| `FluidPort_a`, `FluidPort_b` | acausal | ring2 | 12 | 2.0 | |
| `RealInput`, `IntegerInput`, `BooleanInput` | input | triangle | 8 | 1.2 + arrow | |
| `RealOutput`, `IntegerOutput`, `BooleanOutput` | output | circle (filled) | 8 | 1.2 | |

**Wire rules:**
- Acausal ports connect only to acausal ports of the same connector class family (`Pin` family counts as one; `Flange_a`/`Flange_b` count as one). Acausal connections are bidirectional.
- Causal ports connect output → input only; input → input and output → output are rejected with clear word messages (e.g. `an output joins only an input`). Inside connectors of a composite model connect through.
- Multiple wires may meet at one port; 3 or more wires render a 4px filled junction dot.

## squero/0

Squero Studio OM communicates with its server (`squero-om` / `paperboat squero`) over a dedicated WebSocket using protocol `squero/0`, distinct from `paperboat/0`.

The server hosts one long-lived `omc` process with serialized execution. Plain expressions pass through `allowed()` which permits only safe model introspection and mutation calls (`loadModel`, `loadFile`, `getClassNames`, `getComponents`, `getIconAnnotation`, `getDiagramAnnotation`, `getConnectionCount`, `getNthConnection`, `checkModel`, `addComponent`, `deleteComponent`, `addConnection`, `deleteConnection`, `setComponentModifierValue`, `list`, `dumpXMLDAE`, `buildModelFMU`, `getVersion`, etc.). Arbitrary system commands (`system()`, `runScript()`) are rejected.

### Status Pushes

On connection and whenever compiler state changes, the server pushes:
```jsonc
{ "op": "status", "state": "ready" | "checking" | "failed", "version": "1.27.0~dev-5-g5f1d2c5" }
```

### Operations

Each request is an object with an `id` and `op`; replies return `{ id, ok: true, result }` or `{ id, ok: false, error: "…" }`.

- **`omc`**: Evaluates an allowed OpenModelica expression:
  - Request: `{ "id": 1, "op": "omc", "expr": "getClassNames()" }`
  - Reply: `{ "id": 1, "ok": true, "result": "{Modelica}" }`
- **`open`**: Opens the target `.mo` file specified at startup (default `./Untitled.mo`):
  - Request: `{ "id": 2, "op": "open" }`
  - Reply: `{ "id": 2, "ok": true, "result": { "file": "Untitled.mo", "model": "Untitled" } }`
- **`save`**: Saves the active user model to its `.mo` file on disk (`save(model)`). Refuses the Modelica Standard Library (Studio edits only user files):
  - Request: `{ "id": 3, "op": "save", "model": "Untitled" }`
  - Reply: `{ "id": 3, "ok": true, "result": { "file": "Untitled.mo" } }`
- **`icon`**: Retrieves the imported visual icon, connectors, and conversion notices for a Modelica class (`paperboat modelica icon`):
  - Request: `{ "id": 4, "op": "icon", "class": "Modelica.Electrical.Analog.Basic.Resistor" }`
  - Reply: `{ "id": 4, "ok": true, "result": { "icon": { ... }, "connectors": { ... }, "notice": null } }`
- **`set_icon`**: Converts an `IconDef` into a Modelica `Icon(graphics)` annotation (the inverse of the importer; updates `g` and plate size):
  - Request: `{ "id": 5, "op": "set_icon", "model": "Untitled", "icon": { ... } }`
  - Reply: `{ "id": 5, "ok": true, "result": {} }`
- **`generate`**: Generates a Paperboat C++ backend (`<model>-cpp/`) via ODE compilation or FMU export. When `dry` is true, writes nothing to disk and returns virtual files in `result.files`. Emits streaming progress notifications (`{ "op": "progress", "id": 6, "stage": "check"|"flatten"|"generate"|"build", "fraction": 0.0..1.0 }`) before the final reply:
  - Request: `{ "id": 6, "op": "generate", "source": "Untitled.mo", "model": "Untitled", "out": "untitled-cpp", "dry": true }`
  - Reply: `{ "id": 6, "ok": true, "result": { "model": "Untitled", "path": "ode", "reason": "ODE: no events, no torn systems, no external functions", "out": "untitled-cpp", "notices": [], "files": { ... } } }`
- **`simulate`**: Runs a quick check simulation with omc (500 intervals) and returns time and variable series:
  - Request: `{ "id": 7, "op": "simulate", "model": "Untitled", "stop": 1.0, "vars": ["R1.i", "R1.v"] }`
  - Reply: `{ "id": 7, "ok": true, "result": { "t": [0.0, ...], "series": { "R1.i": [...], "R1.v": [...] }, "names": ["R1.i", "R1.v", ...] } }`

## Station objects

A catalog may list station object types next to its blocks. A station object lives on the
Station page; its properties (initial conditions included) are edited there only. One
that spawns a **linked block** gets a block on the Flow page with the same id: the flow
pushes on the object through the block's inputs and reads it through its outputs.

```jsonc
// catalog
{
  "type": "catalog",
  "blocks": [{ "type": "cart-pole", "label": "Cart-pole", "kind": "source",
               "inputs": [{ "id": "force", "type": "signal" }],
               "outputs": [{ "id": "theta", "type": "signal" }], "params": [],
               "group": "control", "linked": true }],          // not in the palette
  "objects": [{ "type": "cart-pole", "label": "Cart-pole", "role": "body", "group": "cart-pole",
                "props": [{ "id": "theta0", "type": "number", "default": 0.15 }],
                "links": [], "block": "cart-pole" }]          // links: slots naming other objects
}

// The catalog may also carry the Dashboard's default layout (see below)

// Station: station.load, station.set, graph.run
{ "objects": [{ "id": "cart-pole-1", "type": "cart-pole", "props": { "theta0": 0.15 } }] }
// links: slot → the id of the object filling it; pos: its place in the Station's graph view
{ "objects": [{ "id": "mass-1", "type": "mass", "props": { "x0": 1 } },
              { "id": "spring-1", "type": "spring", "links": { "a": "wall", "b": "mass-1" }, "pos": [220, 0] }] }
```

**Variables** (#36). Named, typed values defined once and used by reference (above):

```jsonc
[{ "name": "pole_length", "type": "number", "value": 1, "unit": "m", "min": 0.2, "max": 3, "group": "Cart-pole" },
 { "name": "kp_theta", "type": "number", "value": 40, "unit": "N/rad" },
 { "name": "kd_theta", "type": "number", "expr": "2 * sqrt(kp_theta * pole_length)" },  // a formula
 { "name": "controller", "type": "enum", "value": "pid", "options": ["pid", "lqr"] },
 { "name": "trail", "type": "boolean", "value": false, "exposed": true, "dock": true }]          // also a Dashboard and a dock control
```

`type` is `number`, `boolean`, `enum`, `text` or `file` (as params). Each has a `value`
of its type or, for a number or boolean, an `expr` over other variables (evaluated in
dependency order; a loop is an error). `min`, `max` and `whole` constrain a number;
`note`, `group`, `step`, `exposed` (a control in the Dashboard's `controls` tile), `dock` (a control in the run dock) and `tunable` (meant to change during a run) are for the UI. A name is letters, digits and `_`,
not `t`, `pi`, `e` or a function's name. A backend's defaults come from its descriptor's
`[[variables]]`; the user's own live with the project (#34). Headless, `--var name=value`
sets one for a `--run`.

A run keeps the variables it started with, except a `tunable` number or boolean variable
(not a formula; #75): when the UI sends `variables.set` during a run, the backend keeps the list for
the next run, answers with `variables.values` (no `scene`: the run owns it), and the run
gives each tunable variable its new value between two steps. It reaches the `variable` block
(`name` param, a variable's value on a wire) and every number param bound to it, directly or
through a formula, on blocks that take a change live: those with no state (`Block::stateless`,
made again with the new param) and those that implement `Block::set_param` (PID's gains). Any other
block, and a station object's property, takes the new value with the next run; the Console says so
once. A value its `min`/`max` refuses is ignored, with a warning.

A block may set a tunable variable too (#166; an autotuner or a calibration): `Context::set_variable`
(C++) and `ctx.set_variable` (Python) check it as the UI's change is checked (a `tunable`, non-formula
variable, of the value's type, within its range; a refusal warns once in the Console and returns false)
and apply it at the end of the pass, so every block in a pass sees the same variables. Formulas and
bound params follow as above, and the backend sends `variables.patch` so the UI keeps the values in the
project. A `variable` param (a block setting that names a project variable, a picker in the UI) is how a
block is told which one to set.

**Project file** (#82). Everything a user made, as one JSON file (`name.ppj`), written
and read by the UI; it is not a message, and a backend needs no code for it. It reuses the shapes
above: `flow` (a `Graph`, every node with its `pos`), `station`, `variables`, `dashboard` and
`run` (only what differs from the backend's defaults), plus `paperboat` (the format version, `0`;
a newer file is refused) and `backend` (the `engine` it was made on; opening it elsewhere warns,
and blocks the other backend lacks stay as lost blocks). The shape is `Project` in the schema.
See `docs/api/project.rst`.

**Dashboard layout.** The Dashboard is a grid of equal cells with one instrument per monitor
block on the Flow canvas (the canvas decides which exist). A catalog may carry a default
layout, `dashboard`, which the UI uses until the user arranges their own (kept in the
browser per backend, and in the project file). All fields are optional:

```jsonc
"dashboard": {
  "columns": 6,                                       // 1 to 12; the UI fits fewer on a narrow screen
  "instruments": [
    { "block": "graph-theta", "size": "large", "at": [0, 0] },   // at: top-left cell [column, row]
    { "block": "speed", "size": "compact" }           // no place: falls in by the automatic order
  ]
}
```

`size` is `compact` (1×1 cells, a readout), `normal` (2×1, a plot) or `large` (2×2); absent,
the kind's default (display compact, graph normal, image large). An instrument that is not
listed, or has no `at`, takes the first free cell in the automatic order (readouts, then
pictures, then plots, each in Flow-canvas order); a block that is not a monitor is ignored;
a place that is taken makes the instrument move down to the first free cell.

**Decorations and tabs (#65, #76).** The layout may also carry things that are not
instruments, and pages. They belong to the layout, not the flow: the Flow canvas still
decides which instruments exist.

```jsonc
"dashboard": {
  "columns": 6,
  "tabs": ["Run", "Tuning"],                          // two or more show a tab strip; instruments name theirs with "tab"
  "decorations": [
    { "kind": "label", "text": "Speed {speed:.1f} m/s", "at": [0, 0], "span": [4, 1], "style": "heading" },
    { "kind": "section", "text": "Stores", "at": [0, 1], "span": [4, 2] },   // a titled frame, takes no cells
    { "kind": "hline", "at": [0, 3], "span": 6 },     // a separator in the gap: span columns (vline: rows)
    { "kind": "console", "at": [0, 4], "span": [6, 3] },                      // the Console's lines, in a frame
    { "kind": "controls", "at": [0, 7], "span": [4, 3] }                      // a control for each `exposed` variable
  ]
}
```

`label` takes cells like an instrument (default span 2×1), as do `console`, `station` and `controls` (4×3); `section`,
`hline` and `vline` are drawn in the gaps and take none (they are hidden on a screen
narrower than the layout). Text is a formatted string: `{block}` shows that monitor's latest
value ("–" before a run), `{block:.2f}` with that many decimals, `{{` and `}}` are braces.

- The UI keeps one linked block per object that spawns one, adds it when the object is
  added and removes it (and its wires) when the object goes.
- Linked blocks have no params and no feedthrough: outputs come from the object's state;
  inputs push the object after the flow's pass.
- A run fails if a linked block's object isn't in the station.
- **Links** join station objects (a spring's `a` and `b`): each slot the object's type
  declares holds one other object's id. The backend checks them (the slot exists, the
  target is on the station) and hands each object what it links to before a run. When
  `hello.station.view` is `"graph"`, the Station page draws objects as nodes and links as
  wires; removing an object removes the links to it.

## Step flow

A graph whose blocks have `step` ports is a Step flow (see `examples/backends/animation-python`):

- **Hats** are blocks with step outputs and no step input. They start **paths**: Start
  once at the beginning, When each time its signal `cond` rises (a key press). Several
  paths can run at once; a When doesn't restart its own path while it still runs.
- A path is at one block at a time. Each clock step the backend first runs the signal
  blocks (probes, math, monitors) in dependency order, then ticks the hats, then lets
  each path's block decide: leave by one of its step outputs now (instant: If, Repeat)
  or stay and work (Wait, Move). A path may pass several instant blocks in one clock
  step; a loop with none that takes time fails the run. A path reaching a block another
  path is in waits before it.
- Leaving by an unwired step output ends that path; reaching a Stop block ends the run. The run ends (`done`) when no path
  runs and no hat may start one; with a When it runs until `run.cancel` or a time limit.
- `run.progress` can't know the length in advance; it may only send `1` at the end.

## Groups

A graph may carry `groups`: `{ id, kind: "visual" | "subsystem" | "story", label?, nodes: [ids],
collapsed?, pos?, params?: { seconds, reset }, release?: [[node, port], ...],
expose?: [{ side: "in" | "out", to: [node, port] }, ...] }`. A block is in at most one group.
Groups nest (`parent`). `expose` lists ports a subsystem or story shows as a block even where no wire
crosses its edge yet (an input or output of a block inside it); the UI sets it, backends ignore it.

- **visual** tidies the canvas: the backend runs its blocks as if ungrouped. Collapsed,
  the UI draws it as one block whose ports are the wires crossing its edge; the wires
  themselves still join the blocks inside.
- **story** is one step of a Step flow. Its id is a node with step ports `in` and `next`
  (wire Start → a story → the next story). When a path arrives, the story's blocks (signal
  blocks only) run once per clock step for `params.seconds` (0: once), then the path
  leaves by `next`. Its blocks don't run at any other time; what they computed stays on
  their outputs, so later stories read the results. `release` lists outputs to drop when
  the story leaves (useful for a controller's force, while retaining an estimate). With
  `params.reset` its blocks start afresh each time a path arrives (a story in a loop starts
  over). `node.state` reports the story like any step block.

## Web station

With `hello.station.view == "web"`, the Station page shows the backend's own page
(`http://<backend host>:<port><page>`) in a sandboxed iframe. The page loads the bridge
`/paperboat/station.js` from the same backend. The UI relays every backend message to it
(`window.postMessage({paperboat: 0, message})`) and replays `catalog`, the latest `scene`
and `station.load` when the page posts `station.ready`. The page may post back only
`station.set` and `input`, which the UI forwards like its own. See GUIDELINES §6.4.

## Conformance

`protocol/smoke_test.py --port <port>` checks any backend against this spec without
knowing its domain, every message against the schema included. `protocol/golden.py`
runs each example's starter headless (`--run`) and compares the results with its
`expected.json`. Every backend in `examples/backends/` passes both.

## Why JSON, and what comes next

- **Wire format: JSON text.** Every language has it, it is readable in the browser
  devtools, and it maps directly to TypeScript types and `nlohmann::json` in C++.
- **Bulk data (CFD fields, meshes): binary WebSocket frames** with a small JSON header
  message announcing them. Not needed in v0.
- **Source of truth for types: JSON Schema** (`schema/`). The TypeScript types are
  generated from it (`gen_ts.py`); C++ structs could be next.
- **Authoring block definitions:** in code, next to what the block does (C++
  `define_block`, Python class attributes); the descriptor picks and customises them and
  holds the starters. The engine emits them as this JSON.
- **Scripting (later):** embed Lua (via sol2) in the engine for user expression nodes
  and parametric scenes. Lua is small, sandboxable and fast to embed from C++.
