# Epic #160 · Wave 2 · Lane B (`agy` · Argon 4 · effort high)

You are Lane B. Task: the `.mo` importer in C++: connector resolution through `extends`, the
`iconTransformation` edge-slot quantiser, the `Icon(graphics)` → `view.icon` token quantiser,
and passing `view.icon` + `connectors` through the engine so generated backends carry them.

Read first, in this order: `CLAUDE.md`, `specs/issue-160-contracts.md` (rev 2: §0 Wave 2
ownership, §3, §4, §5, §6, §7), `specs/wave-status.toml`, `docs/issue-160-openmodelica.md`
comments 4 and 5, then the Wave 1 code you build on: `engine/modelica/include/paperboat/
modelica/{omc,generate}.hpp`, `engine/modelica/src/{omc,generate}.cpp`,
`engine/tests/modelica_test.cpp`, `engine/toolkit/include/paperboat/toolkit/block.hpp` (`View`).

Hard rule: every framework feature is C++ in `engine/`. Python only binds it. No Python or TS
re-implementation of the importer.

## You own (Wave 2)

`engine/**`, `python/**` (bindings for the new fields only), `engine/tests/data/modelica/**`.
Do not touch `packages/shared/**`, `apps/**`, `protocol/schema/**`, `protocol/gen_ts.py`
(Lane A). Need a schema change: append a `[[requests]]` entry to `specs/wave-status.toml`.

## Step 0: preconditions

- Work in a new worktree on branch `epic160/lane-b-w2` from `dev`.
- `dev` must contain both Wave 1 merges (`engine/modelica/` and `packages/shared/` exist). If
  not: set `[lanes.B]` `state = "blocked"`, `blocker = "wave 1 not integrated"`, stop.
- Set `[lanes.B]`: `wave = 2`, `model = "argon-4"`, `effort = "high"`, `state = "active"`,
  `branch`, `task` (from `[routing].wave2.b`).
- `omc` is at `/usr/bin/omc` (1.27 dev). Use the Wave 1 `paperboat::modelica::Omc` client and
  `parse_value()`; do not spawn `omc` another way.

## Step 1: connector resolver (`engine/modelica/src/connectors.cpp`)

- For a class, list its components; for each whose type is a connector, walk `extends` to the
  base connector; read each variable's prefix: `flow` → flow, `stream` → stream, else potential
  (with `unit`); `input`/`output` prefix or `RealInput`/`RealOutput` families → causal kind.
- Map to the built-in table in contract §4 (handle, size, wire weight, variant from `_a`/`_b`,
  `p`/`n`). Unknown user connector: nearest built-in handle by kind and domain, recorded in the
  notice.
- Keep array dimensions (`Pin p[n]` → `array: "n"`) and conditions (`if useHeatPort` →
  `if: "useHeatPort"`).

## Step 2: `iconTransformation` quantiser

- Input: extent, origin, rotation, mirror from `Placement(iconTransformation(...))` (fall back to
  `transformation`) in the -100..100 OMEdit box.
- Output: `{side, at}`: nearest edge of the icon box; `at` = position along that edge mapped to
  0..100 and rounded to 5. Rotation snaps to 90°; mirroring kept.
- Collisions after snapping (same side + at): spread evenly across the free slots on that side;
  record every moved port and the distance moved.

## Step 3: `Icon(graphics)` quantiser

- Parse `getIconAnnotation` output with `parse_value()`. Map `Line→line`, `Polygon→poly`,
  `Rectangle→rect` (radius → corner sm/md), `Ellipse→ellipse` (non-full angles → `arc`),
  `Text→text` (`%name`/`%R` style → `{name}`/`{R}`).
- Coordinates: -100..100 → 0..100, y flipped, snapped to 5.
- Colours/thickness → tokens only (contract §3, §7): dark → `ink`, light fill → `raised`,
  mid grey → `muted`; `thickness >= 0.5` → `wire`, `0.25..0.5` → `normal`, else `thin`;
  `LinePattern.Dash/Dot` → `axis`; patterns, gradients → plain fill; `Bitmap`,
  `DynamicSelect` dropped. Never emit a colour, hex or path string.
- Plate size: `1x1` unless the icon's extent aspect is ≥ 1.75:1 (→ `2x1` or `1x2`).
- Notice: `{ simplified: n, moved: [...], dropped: [...] }` per class.

## Step 4: engine passthrough and generator

- `BlockDef::View` gains `icon` as an opaque `nlohmann::json` (merged by `overridden_by` like the
  other keys); descriptors (TOML and C++) gain top-level `connectors`; both reach `hello` /
  `catalog` JSON unchanged and the Python bindings. Wait for Lane A's schema `$defs` (a
  `[[requests]]` entry marked done with a sha); until then code against contract §3/§4 types.
- Generator (`paperboat modelica generate`): the linked block gets `view.icon` from the top-level
  model's Icon and `ports` for its causal connectors; the descriptor gets the `connectors`
  table. Regenerate `examples/dc-motor-cpp/`; its golden must still pass.
- CLI: `paperboat modelica icon <Model.mo|Qualified.Name> [--class X]` prints
  `{icon, connectors, notice}` as JSON (for Studio and for debugging).

## Step 5: tests (`engine/tests/modelica_test.cpp`, tag `[modelica]`)

- Goldens: `engine/tests/data/modelica/{Resistor,Inductor,Capacitor,Ground,Inertia}.icon.json`
  from MSL `Modelica.Electrical.Analog.Basic.*` and `Modelica.Mechanics.Rotational.Components.Inertia`,
  plus a small local `.mo` with `Pin p[n]` and an `if` port. Lane A's renderer test reads these;
  they must match contract §3/§4 types exactly.
- Assert: every value is from the token table; every coordinate is a multiple of 5 in 0..100;
  port sides/at for Resistor (p left 50 filled, n right 50 hollow) and Inertia (flange_a left,
  flange_b right); collision spread test; notice counts.
- Tests needing `omc` skip with a message when it is absent.

## Gate (report all in `[lanes.B].verification`)

`cmake --build engine/build/dev` with no warnings; `paperboat_tests` all pass and
`paperboat_tests '[modelica]'` pass; `python3 protocol/golden.py examples/dc-motor-cpp` PASS;
`paperboat check examples/dc-motor-cpp` (smoke validates `view.icon`/`connectors` against Lane
A's schema once it lands); `npm run test:python`; `npm run docs` with no warnings (document the
importer in `docs/api/cpp.rst` and the `icon` subcommand in `docs/api/tutorials.rst`). Update
`GUIDELINES.md` / `ARCHITECTURE.md` (gitignored, edit in the main tree) for the new
`engine/modelica` pieces and any lessons.

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`; set `state = "review"` with `modified_files` and `verification`, and stop.
