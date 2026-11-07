# Epic #160 · Wave 3 · Lane A (`claude` · Sonnet 5.5 · effort high)

You are Lane A. Task: build `apps/studio`, **Squero Studio: OpenModelica**: the top bar,
the MSL library tree, the acausal diagram canvas, the split view with a local Simulate check,
the `StudioApi` implementation, and the squero/0 server ops they need.

Read first, in this order: `CLAUDE.md`, `specs/issue-160-contracts.md` (rev 3: **§8 first**,
then §1, §3, §4, §5, §6), `specs/wave-status.toml`, `apps/studio/src/model/types.ts` (the
frozen seam), `apps/web/DESIGN.md`, `docs/issue-160-openmodelica.md` comments 1, 5 and 6.
Then the code you build on: `apps/studio/src/{App.tsx,omc/omc.ts}`,
`packages/shared/src/{index.ts,components.ts,Icon.tsx,ports.ts,Compact.tsx,Shell.tsx,Dock.tsx}`,
`engine/modelica/include/paperboat/modelica/{omc,generate,import,squero}.hpp`.

## You own (Wave 3)

Contracts §8.1, Lane A rows: `apps/studio/{package.json,tsconfig.json,vite.config.ts,
index.html}`, `apps/studio/src/{main.tsx,App.tsx,styles.css}`,
`apps/studio/src/{shell,library,canvas,split,simulate,omc,store}/**`,
`apps/studio/test/a-*.test.ts`, `engine/**`, `bin/**`, `apps/cli/**`.

Never edit `apps/studio/src/{popover,sheet,dock}/**` (Lane B) or `src/model/types.ts`
(frozen). `packages/shared`, `apps/web`, `protocol` are frozen: append a `[[requests]]` entry to
`specs/wave-status.toml`. If the seam is missing something you need, request it; don't widen it.

## Step 0: preconditions

- New worktree, branch `epic160/lane-a-w3` from `dev` HEAD.
- Check `dev` has `apps/studio/src/model/types.ts` and the three B slot stubs; if not, set
  `[lanes.A]` `state = "blocked"` with the reason and stop.
- Set `[lanes.A]`: `state = "active"`, `branch`.

## Step 1: squero/0 ops (C++, `engine/modelica`, first commit)

Per §8.2: `icon`, `generate` (with `progress` pushes and `dry`), `set_icon` (IconDef →
Modelica `Icon(graphics)`, the inverse of the Wave 2 quantiser, round-trips its goldens),
`simulate` (`t` + chosen series). `squero-om` serves `apps/studio/dist` by default when it
exists. Tests in `engine/tests/modelica_test.cpp` (`[squero]` tag), omc tests skip without
omc. Document the ops in `engine/modelica/include/paperboat/modelica/squero.hpp`; Wave 4 moves
them into `protocol/README.md`.

## Step 2: shell and store

- Top bar: `squero studio [ OM ]` (lowercase wordmark, light `studio`, rounded ink `Om` badge),
  right side `chip(status)` (`omc 1.27 · ready`). Reuse shared `Shell`/`Menus` where they fit.
- `src/store/`: implements `StudioApi`; every edit is an omc mutation (`addComponent`,
  `addConnection`, `setComponentModifierValue`, ...) followed by a re-read, so the `.mo` stays
  OMEdit-compatible. Undo via shared `undo.ts`, one step per user action.
- `App.tsx` provides `StudioContext`, mounts B's slots exactly as the stubs declare: popover on
  component click, sheet on `Alt+V`, dock always.

## Step 3: library tree

`getClassNames(..., recursive=false)` lazily per package (MSL + loaded local files), filter
box, each leaf with its icon from the `icon` op (cache per class). Drag or `/` to place.

## Step 4: acausal canvas

- `@xyflow/react`, shared `Compact` + `Icon` + port rendering from `view.icon.ports` and the
  `connectors` table.
- Acausal wires undirected (start at either end), causal wires directed with an arrow; validity
  from shared `ports.ts`; a refused wire shows its word message, never silently connects.
- Junction dots at 3+ wires; `Line(points)` waypoints kept on the 20px grid.
- `/` quick-add (reuse the shared browser pattern): on a selected wire, splice: `Insert R2:
  R1.n -> p, n -> L1.p` (delete one connection, add component, add two connections) as one
  undo step; the line shown before confirm is exactly that text.
- Conditional (`if`) ports and arrays behave as shared already draws them.

## Step 5: split view + Simulate

- Toggle `[ .mo ] causal ODE c++`: `.mo` from `list()` (read-only this wave), C++ from
  `generate` with `dry: true` (show `path` + `reason` above the code).
- Simulate: stop time + variable picks, uPlot plot via the `simulate` op. A local check only;
  no blocks, no Flow page, no Board.

## Step 6: tests

`apps/studio/test/a-*.test.ts` (node --test, like web): store maps actions to the right omc
expressions; splice produces exactly the 3 mutations; validity messages; chip text. Engine tests
for the ops. `npm run typecheck`, `npm run build:studio`.

## Gate (report all in `[lanes.A].verification`)

Engine build no warnings; `paperboat_tests` all pass incl. `[squero]` and `[modelica]`;
`npm run typecheck`; `npm run build`; `npm run build:studio`; `npm test -w @paperboat/web`,
`-w @paperboat/shared`, `-w @paperboat/studio`; `npm run docs` no warnings (Studio section in
`docs/api/user_manual.rst`: library, canvas, split view, Simulate). Live check through
`squero-om`: build a Resistor-Inductor-Ground circuit with a `RealInput` voltage source,
splice a resistor in, view `.mo` and C++, simulate; light and dark screenshots under
`specs/evidence/wave3-a/`. Update `GUIDELINES.md` / `ARCHITECTURE.md` (gitignored, main tree)
for `apps/studio` and the new ops.

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`. When done set `[lanes.A]` `state = "review"` with `modified_files`,
`verification`, `notes`, and stop.
