# Epic #160 · Wave 3 · Lane B (`agy` · Argon 4 · effort medium)

You are Lane B. Task: three Studio panels in `apps/studio`: the **Component popover**, the
**Interface sheet** (with the **Look** tab), and the **Dock** (balance, `[ Fix: Ground ]`,
`Export backend`).

Read first, in this order: `CLAUDE.md`, `specs/issue-160-contracts.md` (rev 3: **§8 first**,
then §3, §4, §6), `specs/wave-status.toml`, `apps/studio/src/model/types.ts` (the frozen seam:
your only way to read and change the model), `apps/studio/src/{App.tsx,popover,sheet,dock}`
(the slot stubs you replace), `apps/web/DESIGN.md`, `docs/issue-160-openmodelica.md`
comments 1, 4 and 5. Then the shared pieces you use: `packages/shared/src/{components.ts,
Icon.tsx,icon.ts,Compact.tsx,NumberField.tsx,Dock.tsx,tokens.css}`.

## You own (Wave 3)

Contracts §8.1, Lane B rows only: `apps/studio/src/{popover,sheet,dock}/**` (each with its own
`.css`, imported by its own components) and `apps/studio/test/b-*.test.ts`,
`apps/studio/test/fake.ts`.

Never edit `App.tsx`, `main.tsx`, `styles.css`, `src/model/types.ts`, `package.json` or any
other Lane A path; never `packages/shared`, `apps/web`, `protocol`, `engine`. Keep the three
exported signatures exactly as the stubs declare them. Need anything else (a seam method, a
shared tweak, a dependency): append a `[[requests]]` entry to `specs/wave-status.toml` and code
against the contract meanwhile.

Lane A builds the canvas, store and squero ops in parallel; you will not see them until merge.
Develop and test against `test/fake.ts`, a fake `StudioApi` (in-memory model, scripted
`checkModel` replies, scripted `ExportProgress`).

## Step 0: preconditions

- New worktree, branch `epic160/lane-b-w3` from `dev` HEAD.
- Check `dev` has `apps/studio/src/model/types.ts` and the three stubs; if not, set
  `[lanes.B]` `state = "blocked"` with the reason and stop.
- Set `[lanes.B]`: `state = "active"`, `branch`.

## Step 1: `test/fake.ts`

A `StudioApi` over an in-memory `StudioModel` (a Resistor, an Inductor, no Ground, one
`RealInput`), recording every call. Plus a tiny React test harness if you need one; prefer
testing pure functions (balance parsing, dangling detection, Look edits) with `node --test`.

## Step 2: Component popover (`src/popover/`)

- Opens next to the component (A mounts it on click; `onClose` on Esc / outside click). No modal.
- One row per param: name, shared `NumberField`, unit in grey mono; commit → `setParam("R1.R", v)`.
- `=` on a row binds it to a top-level parameter: name prompt inline, then `setParam` with the
  modifier text `R = R_load` and the top-level default.
- Port list with `+ Input` (causal inputs) and `+ Probe` (outputs and variables) → `addEntry`.

## Step 3: Interface sheet (`src/sheet/`)

- Slides in when `open` (A binds `Alt+V`). Tabs: **Interface**, **Look**.
- Interface: Station props (top-level `model.params`, editable defaults via `setParam("J", v)`),
  entry pills (inputs left, outputs right, unit grey, `×` → `removeEntry`), and the linked block
  preview: shared `Compact` + `Icon` drawing `model.icon` with the entries as ports, the way
  Paperboat will draw the generated linked block.
- Look: canvas on the 0–100 box with the 5-unit snap grid; keys for `line`, `poly`, `rect`,
  `ellipse`, `arc`, `text`; select/move/delete; stroke / weight / fill / dash / corner as
  `Choice` controls listing the §3 tokens only (no colour picker, no free numbers); plate size
  `1x1 | 2x1 | 1x2`; port placement (`side`, `at`) for the entries. Preview of plate light and
  dark side by side. Save → `setIcon(icon)`. Output must pass shared `icon.ts` validation; a
  test asserts it.

## Step 4: Dock (`src/dock/`)

- Balance: after each model change (debounced 300 ms), `omc.call('checkModel(<model>)')`,
  parse "N equation(s) and M variable(s)" → `equations / unknowns`, balanced in ink, unbalanced
  says which way in words.
- Dangling flow connectors: from `model.connections` + `model.connectors` (a port whose class
  has `flow` variables and no connection) → `L1.n unconnected [ Fix: Ground ]`. Fix by domain:
  electrical → `Modelica.Electrical.Analog.Basic.Ground`, translational →
  `Modelica.Mechanics.Translational.Components.Fixed`, rotational →
  `Modelica.Mechanics.Rotational.Components.Fixed`; others show no button. Fix =
  `addComponent` near the port + `connect`.
- `Export backend`: output folder field (default `<model>-cpp`), button →
  `exportBackend(out, onProgress)`; a 2px `--signal` progress line across the dock top driven
  by `fraction`, stage word beside it; result line `ode · no events …` or the error in words.
  Disabled with the reason when unbalanced.

## Step 5: tests

`apps/studio/test/b-*.test.ts`: balance parse (balanced / over / under / omc error), dangling
detection and fix choice per domain, Look output validates and snaps to 5, popover `=` produces
the right `setParam` calls, export progress sequence. All against `fake.ts`.

## Gate (report all in `[lanes.B].verification`)

`npm run typecheck`; `npm run build:studio`; `npm test -w @paperboat/studio` (your `b-*`
tests); `git diff --name-only dev...HEAD` lists only your owned paths (paste it). Screenshots
of popover, sheet (both tabs) and dock in light and dark, rendered with `fake.ts`, under
`specs/evidence/wave3-b/`.

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`. When done set `[lanes.B]` `state = "review"` with `modified_files`,
`verification`, `notes`, and stop.
