# Epic #160 · Wave 4 · Lane B (`agy` · Flash 3.8 · effort low)

You are Lane B. Task: docs and CI for epic #160, plus two small shared fixes Lane A asked for.
Most of the doc content already exists (Wave 2 wrote "Custom block look" in both files); your
job is to complete it, add what is missing, and make CI run everything the epic added.

Read first: `CLAUDE.md`, `specs/issue-160-contracts.md` (§1, §3, §4, §5, §8),
`specs/wave-status.toml` (open `[[requests]]`), `apps/web/DESIGN.md`, `protocol/README.md`,
`engine/modelica/include/paperboat/modelica/squero.hpp` (the squero/0 ops),
`.github/workflows/{web,engine,python,docs}.yml`.

## You own (Wave 4)

`apps/web/DESIGN.md`, `protocol/README.md`, `.github/**`, `packages/shared/**` (only the two
fixes below), `apps/web/src/styles.css` + `apps/studio/src/styles.css` (only to move the shared
rules). Nothing in `engine/`, `examples/`, or other `apps/studio` paths (Lane A).

## Step 0

New worktree, branch `epic160/lane-b-w4` from `dev` HEAD. Set `[lanes.B]` `state = "active"`,
`branch`.

## Step 1: `apps/web/DESIGN.md`

- New section **Squero Studio OM** (C6 + contracts §1, §8): name hierarchy (Squero Studio:
  OpenModelica / Squero Studio OM / `squero-om`), top bar `squero studio [ OM ]` with the
  rounded ink `Om` badge and `omc <ver> · ready | checking | failed` chip, the boundary (Studio
  builds equipment, Paperboat runs procedure; Simulate is a local check), and the Studio surfaces
  in the guide's own words: library, acausal canvas (undirected wires, word errors, `/` splice),
  split view, popover (`=`, `+ Input`, `+ Probe`), interface sheet (`Alt+V`) and Look tab, dock
  (balance, `[ Fix: Ground ]`, Export backend with the 2px signal line).
- **Custom block look**: check the existing row against contracts §3/§4 (plate sizes, token
  table, port shapes and sizes, wire weights, junction dot, `if` → dashed red, hit area) and fix
  any drift. Do not restate the token table twice.
- Take wording from the screenshots in `specs/evidence/wave3-{a,b}/`; do not invent UI.

## Step 2: `protocol/README.md`

- `view.icon`: the primitive list with one JSON example each, the token table (stroke, weight,
  dash, fill, corner, text), plate sizes, the 5-unit grid, the fallback rule. Check the existing
  section; complete it rather than duplicating it.
- `connectors`: the `ConnectorDef` fields and the built-in class table from contracts §4; wire
  rules; `IconPort` (`side`, `at`, `variant`, `array`, `if`).
- New section **squero/0** (the accepted request): Studio ↔ Studio server, separate from
  `paperboat/0`; `omc` op and the allow-list rule; `status` pushes; Wave 3 ops `open`, `save`,
  `icon`, `set_icon`, `generate` (`dry`, `progress` pushes), `simulate`, copied from
  `squero.hpp` (that header is the source; keep the README in step with it).
- `npm run docs` with no warnings; if `docs/api/protocol.rst` includes the README, check it renders.

## Step 3: CI (`.github/workflows/`)

- `web.yml`: also `npm test -w @paperboat/shared`, `npm test -w @paperboat/studio`,
  `npm run build:studio`, and `python3 docs/gen_tokens.py --check`.
- `engine.yml`: already builds everything; make sure `engine/modelica` builds without omc
  (its omc tests skip) and the job stays green on a runner with no OpenModelica.
- New `modelica.yml`: installs OpenModelica on ubuntu-24.04 from the official apt repo
  (`build.openmodelica.org/apt`), loads the MSL once (cache it), runs
  `paperboat_tests '[modelica],[squero]'` and the dc-drive golden
  (`python3 protocol/golden.py examples/dc-drive-cpp`) once Lane A lands it (path filter so
  it runs on changes to `engine/modelica/**`, `examples/dc-drive-cpp/**`, `apps/studio/**`).
- Cross-repo: the shared/studio repo split is deferred (contracts §1), so no cross-repo
  workflow yet. Add a short comment in `modelica.yml` saying where it would go, nothing more.
- Validate every workflow with `actionlint` if installed (else `python3 -c "import yaml"` parse)
  and say which you used. You cannot run GitHub Actions here; say so in `notes`.

## Step 4: the two open requests (Lane A → orchestrator, now yours)

- Move the plate / port / wire / menu rules (`.compact-block`, `.plate`, `.glyph`,
  `.react-flow__handle` + `.ip-*`, `.wire-*`, `.menu*`, `.chip`, `.switch`, `.quick-add`) from
  `apps/web/src/styles.css` into `packages/shared/src/plates.css`; export it like `tokens.css`;
  import it in both apps; delete Studio's copied block. Gate: build `apps/web` before and after
  and show the concatenated CSS bundle is byte-identical (`sha1sum`), or explain every difference.
- `packages/shared/src/ports.ts` `connectError`: output → acausal says what an output joins
  ("an output joins only an input"); pin it with a test in `packages/shared/test/ports.test.ts`.

## Gate (`[lanes.B].verification`)

`npm run typecheck`; `npm run build`; `npm run build:studio`; web/shared/studio tests;
`python3 protocol/gen_ts.py --check`; `python3 docs/gen_tokens.py --check`; `npm run docs`
no warnings; workflow validation output; CSS sha1 before/after; `git diff --name-only
dev...HEAD` lists only your owned paths (paste it).

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`. Done: `[lanes.B]` `state = "review"` with `modified_files`,
`verification`, `notes`; stop.
