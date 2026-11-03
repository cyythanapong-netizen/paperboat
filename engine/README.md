# engine

The C++20 engine. See milestone M0 in `docs/plan.html`.

```
engine/
  toolkit/            paperboat::toolkit: reusable flow templates (blocks) and station
                      templates (station objects) in packages: base, control, step; plus
                      the descriptor loader. No domain code.
  core/               paperboat::core: the generic backend. station.hpp (station objects
                      from JSON, scene), graph.hpp (check and sort a graph), run.hpp (a
                      Signal flow run on a clock, events through a callback), backend.hpp
                      (Session: the protocol for one connection; serve(): WebSocket).
  tools/              paperboat-catalog: list templates, print a descriptor's catalog
  tests/              Catch2, including core runs without a network
```

A backend is a project of its own that calls `core::serve(backend)` with a catalog: see
`examples/backends/cart-pole-cpp` and GUIDELINES §6. The engine never builds or reads a
backend; its tests use their own fixtures (`tests/data`). Added with `add_subdirectory`,
it builds only its libraries (`PAPERBOAT_BUILD_TESTS` and `PAPERBOAT_BUILD_TOOLS` default
to on only when it is the top-level project). It serves
`protocol/README.md` and passes `protocol/smoke_test.py`.

Not in the core yet: Step flow runs in C++ (Python has them), caching, node events.

## Build

Needs CMake 3.25+ and a C++20 compiler. nlohmann/json, toml++ and Catch2 come from the
system if installed, otherwise CMake fetches pinned releases; IXWebSocket is always fetched.

```sh
cd engine
cmake --preset dev            # Debug + tests, in build/dev
cmake --build --preset dev
ctest --preset dev
build/dev/tools/paperboat-catalog --templates
build/dev/tools/paperboat-catalog --starters ../examples/backends/cart-pole-cpp/descriptor.toml
```

See `toolkit/README.md` for the templates and the descriptor format.
