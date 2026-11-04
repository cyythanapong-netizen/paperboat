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
| `hello`        | `protocol: "paperboat/0"`, `engine: string`, `station?: { view: "scene" \| "graph" \| "web", page?: string }`, `run?: RunSettings` (its defaults) | on connect. `station.page` (web view only) is a path on the backend's own port, over HTTP |
| `catalog`      | `blocks: BlockDef[]`                        | on connect                   |
| `scene`        | `entities: Entity[]`, `view?: View`         | on connect, and on reset     |
| `graph.load`   | `graph: { nodes, edges }`                   | on connect (starter graph)   |
| `station.load` | `station: Station`                          | on connect, after `graph.load`, if the catalog has `objects` |
| `variables.load` | `variables: Variable[]`                   | on connect, before `graph.load`, if the backend has default variables (its descriptor's `[[variables]]`) |
| `variables.values` | `values: { name: value }`, `error?: string`, `at?: string` | on connect and after each `variables.set`: what every variable comes to (a formula's result), or why they don't hold (`at`: the variable or block at fault) |
| `scene.patch`  | `entities: ({id} & Partial<Entity>)[]`      | during a run                 |
| `run.started`  | `run: string`                               | when a run starts            |
| `run.progress` | `run: string`, `fraction: number` (0–1)     | during a run                 |
| `signal`       | `node: string`, `t: number`, `v: number \| string` | per sample, for monitor nodes. `t` is the x value: time in a simulation, list index otherwise; `v` is text for a display or an image's data URL |
| `node.state`   | `node: string`, `state: "running" \| "done" \| "failed"` (later also `queued`, `cached`, `cancelled`) | A Step flow sends `running` when a path arrives at a block and `done` when it leaves; a run-once sends each block `running` then `done` as it evaluates them; any run sends `failed` for the block that failed it (an unknown type, an error), which the UI marks |
| `node.progress` | `node: string`, `fraction: number` (0–1) | How far a block's long step has got (`Context::progress`), at most ~20 a second per block; the UI shows it on the running block |
| `log`          | `level: "info" \| "warn" \| "error"`, `text`, `node?: string` | any time. `node`: the block or station object that wrote it, or that failed |
| `run.finished` | `run: string`, `status: "done" \| "failed" \| "cancelled"` | when a run ends |

## UI → Backend

| `type`       | Fields                     |
| ------------ | -------------------------- |
| `graph.run`  | `graph: { nodes, edges }`, `station?: Station` (the backend's current one if absent), `settings?: RunSettings` (overrides the backend's defaults for this run), `variables?: Variable[]` (the backend's current ones if absent) |
| `station.set` | `station: Station`: an edit on the Station page; the backend checks it and answers with a `scene` (or a `log` error) |
| `variables.set` | `variables: Variable[]`: the user's variables changed; the backend checks them, keeps them for the next run and answers with a `scene` (the station they shape), or a `log` error |
| `run.cancel` | —                          |
| `input`      | `key: string` (a `KeyboardEvent.code`, e.g. `"Space"`), `down: boolean` |

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
  // { "type": "text" | "key" | "file", "default": "…" },
  // { "type": "enum", "options": ["in-out", "in"], "default": "in-out" } (a choice; the block gets its index)
  "group": "base",                 // optional palette group: the domain, or a toolkit package
  // optional, how to draw it (a hint: absent, or a UI that ignores it, draws the normal card).
  // compact: a small plate with a glyph, bare port dots (names on hover) and a caption with the
  // block's name and value. glyph: a curve (gain, plus, step, clamp, const) or a few characters
  // drawn as text; value: an expression of the params. Every key is optional.
  "view": { "compact": true, "glyph": "gain", "value": "{k}" },
  "feedthrough": false,            // optional: its outputs don't depend on this step's inputs
                                   // (an integrator, a delay), so a feedback loop may pass through it
  // optional, a magic block: one form per way to use it, each with its own ports. Its enum
  // param "form" picks one (the first by default); inputs/outputs are the first form's.
  // A node takes the ports of its form, and the engine makes the block that form needs.
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
{ "mode": "2d", "center": [6, 5], "height": 11 }   // orthographic, looking down −z

// RunSettings: hello.run (the backend's defaults) and graph.run.settings (this run's
// changes; any but mode). A value out of range fails the run with a log, not the backend.
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
in a Step flow), a value shape (`image`, `points`, `numbers`, …) or `any` (any value but a
step). A port, and a number param, may have a `unit` (`"N"`, `"m"`, `"rad"`). A wire joins
ports of one type (`any` joins anything but a step), in one unit when both ends have one:
the UI enforces it while dragging, and a backend fails a run whose wires break it. A signal
input takes one wire; a step output leads to one block. A run may step a clock (simulation: `scene.patch` per frame) or evaluate
once (parametric: one `scene` at the end); the UI handles both.

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
[{ "name": "pole_length", "type": "number", "value": 1, "unit": "m", "min": 0.2, "max": 3, "group": "Plant" },
 { "name": "kp_theta", "type": "number", "value": 40, "unit": "N/rad" },
 { "name": "kd_theta", "type": "number", "expr": "2 * sqrt(kp_theta * pole_length)" },  // a formula
 { "name": "controller", "type": "enum", "value": "pid", "options": ["pid", "lqr"] },
 { "name": "trail", "type": "boolean", "value": false, "exposed": true }]          // also a Dashboard control
```

`type` is `number`, `boolean`, `enum`, `text` or `file` (as params). Each has a `value`
of its type or, for a number or boolean, an `expr` over other variables (evaluated in
dependency order; a loop is an error). `min`, `max` and `whole` constrain a number;
`note`, `group`, `step` and `exposed` are for the UI. A name is letters, digits and `_`,
not `t`, `pi`, `e` or a function's name. A backend's defaults come from its descriptor's
`[[variables]]`; the user's own live with the project (#34). Headless, `--var name=value`
sets one for a `--run`.

**Dashboard layout.** The Dashboard is a grid of equal cells with one instrument per monitor
block on the Flow canvas (the canvas decides which exist). A catalog may carry a default
layout, `dashboard`, which the UI uses until the user arranges their own (kept in the
browser per backend for now, with the project later, #34). All fields are optional:

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

A graph may carry `groups`: `{ id, kind: "visual" | "story", label?, nodes: [ids],
collapsed?, pos?, params?: { seconds, reset } }`. A block is in at most one group.

- **visual** tidies the canvas: the backend runs its blocks as if ungrouped. Collapsed,
  the UI draws it as one block whose ports are the wires crossing its edge; the wires
  themselves still join the blocks inside.
- **story** is one step of a Step flow. Its id is a node with step ports `in` and `next`
  (wire Start → a story → the next story). When a path arrives, the story's blocks (signal
  blocks only) run once per clock step for `params.seconds` (0: once), then the path
  leaves by `next`. Its blocks don't run at any other time; what they computed stays on
  their outputs, so later stories read the results. With `params.reset` its blocks start
  afresh each time a path arrives (a story in a loop starts over). `node.state` reports
  the story like any step block.

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
