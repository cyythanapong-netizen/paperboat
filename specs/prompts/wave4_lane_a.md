# Epic #160 · Wave 4 · Lane A (`claude` · Sonnet 5.5 · effort medium)

You are Lane A. Task: the end-to-end golden probe. Build the DC drive (an electro-mechanical
DC motor driving a rotational inertia through a spring-damper) **in Squero Studio OM**, export
it to a C++ Paperboat backend, and prove it in Paperboat: CMake build, `paperboat check`,
`smoke_test.py`, and a closed-loop `control/pid` + `dashboard/graph` flow with a golden
`expected.json`. This closes epic #160's "Example Probe" and Phase 2/3 done-whens.

Read first: `CLAUDE.md`, `specs/issue-160-contracts.md` (§1, §3, §4, §6, §8),
`specs/wave-status.toml` (`waves.2.verified` "Watch" note, `waves.3`),
`docs/issue-160-openmodelica.md` "Done When", `examples/dc-motor-cpp/` (the Wave 1 generator
example: same plant, written by hand as `.mo`), `protocol/golden.py`, `protocol/smoke_test.py`.

## You own (Wave 4)

`examples/dc-drive-cpp/`, `examples/dc-motor-cpp/` (retire it, below), `engine/**`,
`apps/studio/**` except `src/{popover,sheet,dock}/**` (bug fixes found on the way only),
`apps/studio/test/a-*.test.ts`, `docs/api/tutorials.rst`. Not `apps/web/DESIGN.md`,
`protocol/README.md`, `.github/**`, `packages/shared/**` (Lane B): `[[requests]]`.

Name: the generator names backends kebab-case (`backend_name("DCDrive")` = `dc-drive`,
GUIDELINES §2), so the folder is `examples/dc-drive-cpp/`, not `dc_drive-cpp`.

## Step 0

New worktree, branch `epic160/lane-a-w4` from `dev` HEAD. Set `[lanes.A]` `state = "active"`,
`branch`.

## Step 1: author it in Studio

Through `squero-om examples/dc-drive-cpp/modelica/DCDrive.mo` (headless browser driving the
real UI, as in Wave 3; script kept under `apps/studio/test/e2e/` if it is reusable, else in
your scratchpad): place SignalVoltage, Resistor, Inductor, RotationalEMF, Ground, rotor
Inertia, SpringDamper, load Inertia, Speed and Angle sensors from the library; wire them; `=`
binds R, L, k, J_motor, J_load, c, d to top-level parameters (popover); `+ Input` on the
voltage source's `v`, `+ Probe` on the sensors (outputs `w`, `phi`) and the armature current;
draw a motor-and-load look in the Look tab (2x1); balance reads equal in the dock;
`Export backend` to `examples/dc-drive-cpp/`. Anything you can't do through the UI is a Studio
bug: fix it (your paths) or record it in `notes` and `[[requests]]` (B's paths), then finish
that step by hand in the `.mo` and say so.

## Step 2: prove the backend

- `cmake` build of `examples/dc-drive-cpp/` with no warnings; links only `paperboat::core` +
  `paperboat::toolkit` (no OM at runtime: `ldd` shows no OpenModelica library).
- Open-loop check against omc: a voltage step, `w` and `phi` at 3 times match `omc simulate`
  to 1e-5 (record the numbers).
- `paperboat check examples/dc-drive-cpp` all stages ok and `python3 protocol/smoke_test.py`
  passes. Resolve the Wave 2 "Watch": the catalog stage printed `0 blocks, 0 station objects`
  and warned `no block type "dc-motor"` for the generated backend. Find why (static TOML
  catalog vs objects registered in C++), fix it in the generator or the check, and show the
  catalog now lists the object and its linked block.
- The linked block carries the Look-tab `view.icon` and the `RealInput`/`RealOutput`
  connectors; open the backend in Paperboat (`paperboat open`/`run`) and screenshot the Flow
  page showing the drawn plate (light and dark) under `specs/evidence/wave4-a/`. This is C4's
  "hand-drawn icon survives Export → Paperboat open".

## Step 3: the closed-loop golden

Starter graph in `descriptor.toml`: a `step` setpoint → `control/pid` → the drive's `v`;
`w` back to the PID; `dashboard/graph` on `w`, `i`, and `v`. Tune so `w` settles to the
setpoint without overshoot past 5%. `expected.json` pins the settled values (as dc-motor-cpp
does); `python3 protocol/golden.py examples/dc-drive-cpp` PASS. Add the backend to the golden
set CI runs (request to B for the workflow line if it lives in `.github/`).

## Step 4: retire the duplicate

`examples/dc-motor-cpp/` is the same plant. Delete it and point every reference (tests,
`[modelica]` cases, docs, `package.json` scripts) at `dc-drive-cpp`, unless a test needs the
hand-written `.mo` as a generator fixture: then move just the `.mo` into
`engine/tests/data/modelica/` and say so.

## Gate (`[lanes.A].verification`)

Engine build no warnings; `paperboat_tests` all pass; `npm run typecheck`; `npm run build`;
`npm run build:studio`; studio/web/shared tests; `npm run docs` no warnings (tutorial
"From Modelica to a Paperboat backend" in `docs/api/tutorials.rst` walks the Studio steps
above); `paperboat check` + smoke + golden PASS on `dc-drive-cpp`; open-loop numbers; Flow
screenshots. Update `GUIDELINES.md` / `ARCHITECTURE.md` (gitignored, main tree).

## Rules

Commits: short imperative subject, reference `#160`, no attribution trailers, no push.
Don't merge to `dev`. Done: `[lanes.A]` `state = "review"` with `modified_files`,
`verification`, `notes`; stop.
