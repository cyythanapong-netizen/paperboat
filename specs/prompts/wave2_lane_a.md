# Epic #160 · Wave 2 · Lane A (`claude` · Sonnet 5.5 · effort medium)

You are Lane A. Task: the `view.icon` renderer and first-class Modelica ports in
`@paperboat/shared`, used by `apps/web` (and later `apps/studio`).

Read first, in this order: `CLAUDE.md`, `specs/issue-160-contracts.md` (rev 2: §0 Wave 2
ownership, §3, §4), `specs/wave-status.toml`, `apps/web/DESIGN.md` (Compact block, wires),
`docs/issue-160-openmodelica.md` comments 4 and 5. Contracts win over the issue text.

## You own (Wave 2)

`packages/shared/**`, `apps/web/**`, `protocol/schema/paperboat-0.schema.json`,
`protocol/gen_ts.py`, `docs/gen_tokens.py`, `protocol/README.md`, `apps/web/DESIGN.md`.
Do not touch `engine/**` or `python/**` (Lane B). Need something there: append a
`[[requests]]` entry to `specs/wave-status.toml`.

## Step 0: preconditions

- Work in a new worktree on branch `epic160/lane-a-w2` from `dev`.
- `dev` must contain both Wave 1 merges (`packages/shared/` and `engine/modelica/` exist). If
  not: set your `[lanes.A]` `state = "blocked"`, `blocker = "wave 1 not integrated"`, stop.
- Set `[lanes.A]`: `wave = 2`, `model = "sonnet-5.5"`, `effort = "medium"`,
  `state = "active"`, `branch`, `task` (from `[routing].wave2.a`).

## Step 1: fix the Wave 1 sync gates (land first, own commit)

Wave 1 left both generators pointing at shims:
- `protocol/gen_ts.py`: `OUT` → `packages/shared/src/protocol.ts`.
- `docs/gen_tokens.py`: source → `packages/shared/src/tokens.css`.
- Move the hand-written §3/§4 types out of `packages/shared/src/protocol.ts` into the schema as
  `$defs` (`IconDef`, `Prim`, `IconPort`, `ConnectorDef`), add `view.icon` to `BlockDef.view`
  and top-level `connectors` to the catalogue/hello message where `blocks` already travel.
  Regenerate; the generated file must contain the same types as contract §3/§4.
- Add `builtin: boolean` to `$defs.ToolkitStatus` (open request from Wave 1, fixes smoke).
- Gate: `python3 protocol/gen_ts.py --check`, `python3 docs/gen_tokens.py --check`,
  `npm run typecheck` all green. Commit and tell Lane B through a `[[requests]]` entry
  (`state = "done"`) that the schema `$defs` are on your branch at `<sha>`; B validates against
  them.

## Step 2: `Icon` renderer

- `packages/shared/src/Icon.tsx`: `IconDef` → SVG. 0..100 box scaled to the plate's glyph
  area; plates `1x1` (64x56), `2x1`, `1x2` grow on the 4px grid per DESIGN.md. Round caps and
  joins.
- Token maps are the only source of attribute values: stroke → `var(--text|--line-strong|--signal)`,
  weight → 1 / 1.5 / 2, dash `axis` → the glyph axis dash, fill → `none|var(--text)|
  var(--surface-raised)|var(--surface-sunken)`, corner → `0|sm|5px`, font → `var(--font-sans|
  --font-mono)`, sizes 12/12.5/13/15/30. `{param}` in text is filled like the caption value.
- Validate once per block type; any unknown primitive, token, size or off-grid coordinate →
  return null so `Compact.tsx` draws today's plate; `console.warn` once.
- `Compact.tsx` calls it when `view.icon` exists; else unchanged. No visual change for blocks
  without `view.icon` (built CSS for those paths stays identical; the existing web tests pass).

## Step 3: first-class ports

All in `packages/shared` (`ports.ts`, `portLayout.tsx`, `wireSnap.tsx`, `wireRouting.ts`):
- Lookup: `IconPort.class` → `connectors[class]`; handle shape (`square`, `diamond`, `ring2`,
  `triangle`, `circle`), size 8/10/12, `variant` hollow/filled. Hover shows id plus potential /
  flow / stream names and units in grey mono.
- Placement: `side` + `at` on plates 1x1/2x1/1x2; hit area stays the 40px `wireSnap` radius.
- Wire weight and dash from the class (1.2 / 1.8 / 2.0; thermal dashed grey, never red).
- Connect validity, one function returning a word message or null: acausal ↔ same class family
  only, causal output → input only, input → input rejected. Wrong target is refused with the
  message, never silently allowed.
- Many wires per port; 3+ wires meeting draw a 4px filled junction dot.
- `array`: reuse the #164 vector port model (count badge, fan-out). No second mechanism.
- `if`: evaluate against current params; when false the port is hidden, the edge re-spreads,
  and its wires stay, drawn dashed-red `missing`.
- `apps/web`: only the wiring needed so `FlowEditor`/`StationGraph` pass `connectors` and draw
  the above. Existing signal ports without `view.icon.ports` behave exactly as today.

## Step 4: tests (`npm test -w @paperboat/shared`, add the script)

- Token test: render every primitive with every token; walk the SVG; every attribute value is in
  the token table; feeding a hex, `rgb(`, a `d` path or a raw number for a token fails.
- Fallback test: unknown primitive / token / 2x2 / off-grid → plain plate.
- Port tests: placement per side and size; validity matrix (Pin↔Pin ok, Pin↔Flange refused with
  words, out→in ok, in→in refused); junction dot at 3 wires; `if` toggle keeps wires as missing;
  `array` shows the #164 badge.
- Fixtures: hand-write `Resistor`, `Ground`, `Inertia` icons under
  `packages/shared/test/fixtures/`. When Lane B's goldens land in
  `engine/tests/data/modelica/*.icon.json`, add a test that renders each one (skip if absent).

## Gate (report all of these in `[lanes.A].verification`)

`npm run typecheck`, `npm run build`, `npm test -w @paperboat/web`, `npm test -w
@paperboat/shared`, `python3 protocol/gen_ts.py --check`, `python3 docs/gen_tokens.py --check`,
`npm run docs` (no warnings). Light and dark screenshots of a Flow with one icon block of each
size and a 3-wire node, saved under `specs/evidence/wave2-a/`. DESIGN.md gains a "Custom block
look" section and protocol/README.md documents `view.icon` and `connectors`.

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`; set `state = "review"` with `modified_files` and `verification`, and stop.
