# Epic #160 contracts (Wave 0, locked)

Squero Studio: OpenModelica (Squero Studio OM) and the Modelica-to-C++ backend generator.
Source of truth: pb#160 and its comments; later comments win (C5 replaces C4 §3, C6 names the
product). This file fixes the interfaces both lanes code against. Change it only through the
orchestrator, and bump `contracts_rev` in `wave-status.toml` when you do.

## 0. Lanes and file ownership

A lane edits only what it owns. Anything else: write a request into `wave-status.toml`
`[[requests]]`, don't edit.

Model routing (rev 2): lane A (`claude`) runs Anthropic models only (Opus 5.5, Sonnet 5.5,
Haiku 5.5); lane B (`agy`) runs Google models only (Flash 3.8, Argon 4). Per-wave model and
effort are in `wave-status.toml` `[routing]`.

### Wave 2 ownership (rev 2, replaces the table below for Wave 2)

| Path | Owner |
| --- | --- |
| `packages/shared/**`, `apps/web/**` | A |
| `protocol/schema/paperboat-0.schema.json`, `protocol/gen_ts.py`, `docs/gen_tokens.py` | A (B requests) |
| `engine/**` (importer, `View`/`connectors` passthrough, generator embedding) | B |
| `python/**` (bindings for the new descriptor fields only) | B |
| `engine/tests/data/modelica/**` (importer goldens, read by A's renderer test) | B |
| `protocol/README.md`, `apps/web/DESIGN.md` | A (B requests additions) |

The schema is the seam: A lands the `IconDef` / `ConnectorDef` `$defs` first (§3, §4 types,
unchanged), B's engine output must validate against them (`npm run smoke`).

### Wave 1 ownership

| Path | Owner |
| --- | --- |
| `engine/**` (incl. `engine/modelica/`, `engine/tools/paperboat_cli.cpp`) | A |
| `apps/cli/**`, `bin/squero-om` | A |
| `examples/dc-motor-cpp/` (generated example + golden) | A |
| `apps/studio/src/canvas/**`, `apps/studio/src/omc/**` | A |
| `packages/shared/**` | B |
| `apps/web/**` (rewire to `@paperboat/shared` only) | B |
| `apps/studio/src/{sheet,look,dock,shell}/**`, `apps/studio/package.json` | B |
| `protocol/README.md`, `apps/web/DESIGN.md` | B (A requests additions) |
| root `package.json` (`workspaces`) | B (Wave 1) |
| `GUIDELINES.md`, `ARCHITECTURE.md`, `docs/api/**` | owner of the change, same commit |
| `specs/**` | orchestrator |

## 1. Product and repo boundary

- Names: official **Squero Studio: OpenModelica**, everyday **Squero Studio OM**. Top bar:
  `squero studio [ OM ]` (lowercase wordmark, light `studio`, rounded ink `Om` badge), right
  side chip `omc <ver> · ready | checking | failed`.
- Studio is its own app: `apps/studio` (npm `@paperboat/studio`), launched by `squero-om` and
  `paperboat squero`. Never a page, rail entry or route inside `apps/web`.
- Shared UI: `packages/shared` (npm `@paperboat/shared`). Root `workspaces` becomes
  `["apps/*", "packages/*"]`. Splitting into separate repos (C2) is deferred until the
  in-repo split is stable; nothing in `packages/shared` may import from `apps/*`.
- ISA-88 (GUIDELINES §4): Studio builds equipment and exports `<model>-cpp/`. Paperboat opens
  that backend; procedure (PID, step stories) and Board live in Paperboat. Studio's Simulate is
  a local check, never a second Flow page.

## 2. `@paperboat/shared` (Lane B, Wave 1)

Moves from `apps/web/src/` with history-preserving `git mv`, re-exported from
`packages/shared/src/index.ts`:

`tokens.css, Menus.tsx, Dock.tsx, NumberField.tsx, Compact.tsx, portLayout.tsx, ports.ts,
wireSnap.tsx, wireRouting.ts, Dashboard.tsx, Shell.tsx, Logo.tsx, undo.ts, clipboard.ts,
graphLayout.ts`, plus the protocol types they need (`protocol.ts` types only; the socket code
in `engine.ts` stays in web).

- A file that drags web-only state (stores, `engine.ts`) along is split: the pure part moves,
  the wiring stays. List each split in the PR.
- Done when: `npm run typecheck` and `npm run build` pass; `apps/web` renders identically
  (screenshots of Flow, Station, Board in light and dark before/after; zero diff accepted).

## 3. `view.icon` (Lane B renders, Lane A emits)

Additive to `BlockDef.view`; absent means today's plate. TS types live in
`packages/shared/src/protocol.ts`:

```ts
type Stroke = "ink" | "muted" | "signal";          // --text, --line-strong, --signal (live state only)
type Weight = "thin" | "normal" | "wire";          // 1, 1.5, 2 px
type Dash   = "solid" | "axis";                    // never red: dashed red = missing only
type Fill   = "none" | "ink" | "raised" | "sunken";// --text, --surface-raised, --surface-sunken
type Corner = "0" | "sm" | "md";                   // md = 5px
type Pt = [number, number];                        // 0..100, multiples of 5

type Prim = { stroke?: Stroke; weight?: Weight; dash?: Dash; fill?: Fill } & (
  | { t: "line";    p: Pt[] }
  | { t: "poly";    p: Pt[] }
  | { t: "rect";    x: number; y: number; w: number; h: number; corner?: Corner }
  | { t: "ellipse"; x: number; y: number; w: number; h: number }
  | { t: "arc";     x: number; y: number; w: number; h: number; a0: number; a1: number }
  | { t: "text";    x: number; y: number; s: string; font?: "sans" | "mono";
      size?: 12 | 12.5 | 13 | 15 | 30 }            // s may hold {param}
);

type IconPort = {
  id: string; class: string;                       // key into descriptor `connectors`
  side: "left" | "right" | "top" | "bottom";
  at: number;                                      // 0..100 along the edge, multiple of 5
  variant?: "hollow" | "filled";                   // a/b or p/n side, never state
  array?: string;                                  // param id holding n (#164 vector port)
  if?: string;                                     // param id; port exists while truthy
};

type IconDef = { w?: 1 | 2; h?: 1 | 2; g: Prim[]; ports?: IconPort[] };  // 1x1, 2x1, 1x2 only; 1x1 = 64x56
```

Defaults: stroke `ink`, weight `normal`, dash `solid`, fill `none`, corner `0`, font `sans`,
size `12`. Caps and joins round. 2x2 is invalid.

Rules:
- Unknown primitive, token or size: drop the whole icon, draw the plain plate (hint, not
  error), `console.warn` once per block type.
- Renderer test: every SVG attribute written comes from the token table; a hex, `rgb(`, or
  `d=` path string from input fails the test.
- State (selected, running, failed) is drawn by the plate, never by the icon.
- Source of truth for these types is `protocol/schema/paperboat-0.schema.json`; the TS types are
  generated by `protocol/gen_ts.py` into `packages/shared/src/protocol.ts`, never hand-written.
- Engine: `BlockDef::View` gains `icon` (kept as an opaque `nlohmann::json`, validated by the
  renderer and the schema, not by C++), descriptors gain top-level `connectors`, both passed
  through to `hello` / `catalog` and the Python bindings unchanged.

## 4. Connectors (C5, supersedes C4 §3)

Top-level in the descriptor, sent in `hello`/catalogue next to `blocks`:

```ts
type ConnectorDef = {
  kind: "acausal" | "input" | "output" | "expandable";   // expandable: reserved, not drawn yet
  handle: "square" | "diamond" | "ring2" | "triangle" | "circle";
  alt?: "hollow" | "filled";                              // look of variant "filled"/"hollow" swap
  size: 8 | 10 | 12;                                      // px
  weight: 1.2 | 1.8 | 2;                                  // wire px
  dash?: "solid" | "dashed";                              // thermal wires dashed (grey, not red)
  potential?: { id: string; unit: string }[];
  flow?: { id: string; unit: string }[];
  stream?: { id: string; unit: string }[];
};
type Connectors = Record<string, ConnectorDef>;           // key = Modelica class short name
```

Built-in table (Lane A's importer maps resolved classes to these; user classes quantise to the
nearest built-in handle):

| class | kind | handle | size | wire | variant |
| --- | --- | --- | --- | --- | --- |
| `Pin`, `PositivePin`, `NegativePin` | acausal | square | 8 | 1.2 | p filled, n hollow |
| `Flange_a`, `Flange_b` (transl./rot.) | acausal | square | 10 | 1.8 | a filled, b hollow |
| `HeatPort_a`, `HeatPort_b` | acausal | diamond | 8 | 1.2 dashed | |
| `FluidPort_a`, `FluidPort_b` | acausal | ring2 | 12 | 2.0 | |
| `RealInput`, `IntegerInput`, `BooleanInput` | input | triangle | 8 | 1.2 + arrow | |
| `RealOutput`, `IntegerOutput`, `BooleanOutput` | output | circle (filled) | 8 | 1.2 | |

Wire rules (one function in `packages/shared/src/ports.ts`, returns a word message or null):
- acausal ↔ same connector class only (`Pin` family counts as one class).
- causal: output → input only; input → input rejected; inside connectors of a composite
  connect through.
- Many wires per port allowed; 3+ draw a 4px filled junction dot.
- Hit area 40px (`wireSnap` radius) whatever the drawn size.
- `if` false: port hidden, ports re-spread, its wires kept as dashed-red `missing`.
- `array`: the #164 vector port model (count badge, fan-out). No second array mechanism.

Composite models: Icon layer only in the first cut; open as a group with In/Out pills (#68).

## 5. `omc` bridge (Lane A, Wave 1)

- C++ in `engine/modelica/` (`paperboat::modelica`), one long-lived
  `omc --interactive=zmq -z=<session>` per process; reads the port file omc writes; one
  request in flight (serialised queue); 30 s default timeout; restart on crash, status
  `failed` after 3 restarts.
- Calls used: `loadModel`, `loadFile`, `getClassNames(recursive=true)`, `getComponents`,
  `getIconAnnotation`, `getDiagramAnnotation`, `getConnectionCount`, `getNthConnection`,
  `checkModel`, `addComponent`, `deleteComponent`, `addConnection`, `deleteConnection`,
  `setComponentModifierValue`, `list`, `dumpXMLDAE`, `buildModelFMU`, `getVersion`.
- Studio talks to it over the Studio server's WebSocket, protocol `squero/0`
  (separate from `paperboat/0`): `{id, op: "omc", expr}` → `{id, ok, result | error}`, plus
  `{op: "status", state: "ready"|"checking"|"failed", version}` pushes. Raw expressions go
  only through an allow-list of the calls above (no `system()`, no `runScript`).
- No OM dependency in `engine/core` or `engine/toolkit`; `engine/modelica` is an optional
  CMake target, skipped with a notice when `omc` is not found.

## 6. Generator (Lane A, Wave 1)

CLI: `paperboat modelica generate <Model.mo> [--model <Qualified.Name>] --out <model>-cpp/
[--path fmu|ode|auto]` (default `auto`). Studio's dock "Export backend" calls the same code.
Also `paperboat modelica check <Model.mo>` (wraps `checkModel`).

Path choice (`auto`): ODE when `dumpXMLDAE` gives no events/when-clauses, no nonlinear torn
systems, no external functions/tables; otherwise FMU. The choice and the reason are printed.

Output (GUIDELINES §6.1 layout):

```
<model>-cpp/
  backend.toml  CMakeLists.txt  descriptor.toml  main.cpp
  domain/<model>.hpp  domain/<model>.cpp   # ODE: class <Model> final : paperboat::Model
  fmu/<model>.fmu                          # FMU path only, + domain adapter on StationObject
  modelica/<Model>.mo                      # the source, for round-trip
```

Encapsulation:
- `[[objects]]` one per generated model, `props` = top-level `parameter`s and `start` values
  of states with `fixed=true`, with `unit`; plus `solver` enum (`rk4` default, `rk45`,
  `euler`).
- Linked block (`linked = true`, `feedthrough = false`): inputs = top-level
  `RealInput`/`IntegerInput`/`BooleanInput`; outputs = top-level `*Output` plus variables
  marked `annotation(__paperboat(probe=true))`. Units from the variable's `unit`.
- Everything else stays inside the compiled model. Generated ODE code links only
  `paperboat::core` + `paperboat::toolkit`.
- Generated block carries `view.icon` + `connectors` from Wave 2's quantiser once it lands;
  until then plain plate.

Done when: `examples/dc-motor-cpp/` is generated from a DC motor + inertia + spring-damper
`.mo`, builds, passes `paperboat check` and `npm run smoke`, and a PID flow on it matches
`expected.json` (golden).

## 7. `.mo` import and quantisers (Lane A, Wave 2)

- Connector resolution through `extends` chains → `{class, kind, potential, flow, stream}`.
- `iconTransformation` (extent, origin, rotation, mirror) → `{side, at}`: nearest edge, `at`
  rounded to 5; collisions spread evenly; each moved port recorded.
- `Icon(graphics)` → `IconDef.g`: `Line→line, Polygon→poly, Rectangle→rect, Ellipse→ellipse
  (angles→arc), Text→text`; colours: dark→`ink`, light fill→`raised`, mid grey→`muted`;
  thickness ≥0.5 mm→`wire`; patterns/gradients→plain fill; `Bitmap`, `DynamicSelect` dropped.
- Every lossy step goes into one notice: `{ simplified: n, moved: [port…], dropped: [kind…] }`,
  shown as `n shapes simplified [ Show ]`.
- Tests: MSL `Resistor`, `Inductor`, `Capacitor`, `Ground`, `Rotational.Components.Inertia`,
  plus a model with `Pin p[n]` and an `if` port. Output is validated against §3's types.

## 8. Studio surface (Waves 3+, outline only)

Lane A: library tree (MSL + local), acausal canvas (undirected wires, §4 rules, word errors,
`/` quick-add with wire splicing `Insert R2: R1.n -> p, n -> L1.p`), split view
`[ .mo ] causal ODE c++`, local Simulate plot. Lane B: Component/Interface sheet, Look tab
(§3 primitives, 5-unit snap, no colour picker), Balance dock
(`L1.n unconnected [ Fix: Ground ]`). Detailed Wave 3+ contracts get written before Wave 3
starts (the epic brief was truncated at Wave 3 Lane B).

## 9. Shared rules

- No modal dialogs (C1 §6); errors are words.
- Docs build clean (`npm run docs`), `python3 docs/gen_tokens.py --check` passes.
- Commits: short imperative subject, no attribution trailers, reference `#160`.
- Each lane updates its `[lanes.*]` block in `wave-status.toml` at start, at every blocker,
  and at done.
