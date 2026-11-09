# Contributing to Paperboat

Paperboat is a C++20 engine, Python bindings, a React web app and a Node console. This page
covers how to set it up, build it, run the checks CI runs, and send a change.

## Checkouts

Paperboat spans several repositories that are cloned **side by side**. Some paths are relative
(`file:../../../paperboat-shared` in `apps/web/package.json`), so keep the folder names:

```
work/
├─ paperboat/            this repository
├─ paperboat-shared/     the UI kit (@paperboat/shared). Required: the web app builds against it
├─ paperboat-examples/   the example backends. Optional, and not publicly available: `npm run backend:*`,
│                        `paperboat run <example>` and `protocol/golden.py` need it
└─ squero-studio-om/     Squero Studio (OpenModelica). Optional; scripts/verify.sh checks it
```

```sh
git clone https://github.com/paperboatlab/paperboat.git
git clone https://github.com/paperboatlab/paperboat-shared.git
git clone https://github.com/paperboatlab/squero-studio-om.git   # optional
```

`PAPERBOAT_EXAMPLES_DIR` points the CLI at an examples checkout somewhere else, and
`PAPERBOAT_EXAMPLES_REPO` at another git URL for `paperboat examples fetch`.

## Prerequisites

- CMake 3.25+ and a C++20 compiler (GCC 12+ as in the release build, a recent Clang, or MSVC 2022);
  Ninja is recommended.
  Dependencies (nlohmann/json, toml++, IXWebSocket, Catch2, nanobind) are fetched by CMake.
- Node 22 and npm.
- Python 3.11+, with `pip install websockets jsonschema pytest numpy` for the protocol checks and
  the binding tests.

## Build and test

Each block is what the matching CI workflow runs (`.github/workflows/`).

**Engine** (`engine.yml`): the C++ engine, the `paperboat` CLI and the Catch2 tests.

```sh
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPAPERBOAT_BUILD_TESTS=ON
cmake --build engine/build
ctest --test-dir engine/build --output-on-failure
```

`./bin/paperboat` runs the CLI from `engine/build` (it builds it first if needed).

**Python bindings** (`python.yml`): the `paperboat._engine` extension, built into `python/paperboat`.

```sh
cmake -S python -B python/build -DPython_EXECUTABLE="$(which python3)"
cmake --build python/build
python3 -m pytest -q python/tests
```

**Web app and console** (`web.yml`): needs `../paperboat-shared` installed first.

```sh
(cd ../paperboat-shared && npm ci)
npm ci
npm run typecheck
npm test -w @paperboat/web
npm test -w @paperboat/cli
npm run build
npm run dev            # the web app on http://localhost:3047
```

**Generated files**: `../paperboat-shared/src/protocol.ts` comes from this repository's protocol
JSON Schema, and `docs/_static/tokens.css` from the design tokens. After changing either source,
regenerate them and commit the result; CI fails if they are out of date. The protocol types live
in the `paperboat-shared` repository, so a schema change needs a matching `paperboat-shared` PR
before this repository's web CI can pass.

```sh
python3 protocol/gen_ts.py          # --check: only verify
python3 docs/gen_tokens.py          # --check: only verify
```

**A backend against the protocol**: every backend, in any language, must pass the same
domain-neutral smoke test, and the examples must still match their golden runs.

```sh
./bin/paperboat check engine/tests/sdk-backend   # manifest, catalog, smoke test, golden run
npm run smoke -- 8765                           # the smoke test against a backend already running on a port
npm run golden                                  # every example's starter result (needs paperboat-examples)
```

**Everything at once**: `scripts/verify.sh` runs the checks above for this repository and its
siblings and prints one table; `scripts/verify.sh --quick` runs only typecheck, unit tests and
the generator checks (seconds).

### Platform notes

- **Windows**: build with the Visual Studio generator
  (`cmake -S engine -B engine/build -G "Visual Studio 17 2022" -A x64 -DPAPERBOAT_BUILD_TESTS=ON`,
  then `cmake --build engine/build --config Debug` and `ctest -C Debug`). Only the process-layer
  tests (`engine/tests/process_test.cpp`) build there; the rest of the suite uses POSIX.
- **macOS**: if Apple clang cannot find standard headers such as `<chrono>`, the Command Line
  Tools install is broken; reinstall it (`sudo rm -rf /Library/Developer/CommandLineTools`, then
  `xcode-select --install`).
- **package-lock.json** must list the native packages of every platform (typescript, rolldown,
  lightningcss). If you regenerate it, check that the `darwin-*` and `win32-*` entries are still
  there; a lockfile made with `npm install` on one machine can keep only that machine's.

## Sending a change

- Branch from `dev` (the default branch) and open the pull request against `dev`.
- Work is tracked as GitHub issues with priorities P0–P3; reference the issue in the PR (`#123`).
- Keep a PR to one change, with the tests that cover it. A framework change must not alter an
  example's golden result unless that change is the point of the PR (then say so).
- Commit messages: a short imperative subject (`Build and run on macOS arm64`), then a body
  that says why.
- CI must be green: engine, python, web and docs; release runs too when packaging files change.

## License

Paperboat is licensed under the [Apache License 2.0](LICENSE). By contributing, you agree that
your contributions are licensed under the same terms.
