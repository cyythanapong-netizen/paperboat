# paperboat

A reusable framework for engineering apps that follow one shape:

1. **Setup**: a 2D/3D scene of things, their settings, and how they connect.
2. **Flow**: a node graph (Grasshopper / DaVinci style) that turns the setup into results.
3. **Run**: a 2D monitoring view with progress, live plots, and logs.

First domains: structural analysis, control (cart-pole, double pendulum), and CFD.
No hardware needed. See `docs/plan.html` for the plan.

## Layout

```
apps/web/                 React UI (Vite + TypeScript, React Flow, three.js)
engine/                   C++20 engine: toolkit (reusable blocks and station objects),
                          core (the generic backend), descriptor loader
python/                   Python bindings for the engine (pip install ./python): write a
                          backend's domain code in Python, the C++ engine runs it
protocol/                 WebSocket protocol spec, JSON Schema, smoke_test.py and golden.py
examples/backends/        example backends, each a project of its own that uses Paperboat
                          (copy one anywhere and it still runs). Published separately, as
                          the paperboat-examples repository; clone it next to this one
  mass-spring-python/     simulation: a sine pushes a mass on a spring (Python)
  cart-pole-cpp/          simulation, closed loop: PID balances a pole. C++: the blocks
                          come from the toolkit, the Cart-pole station object is its own
                          code; add more cart-poles on the Station page
  parametric-python/      no simulation: a grid shaped by a wave and an attractor
                          becomes boxes in the scene, Grasshopper style (Python)
  runner-python/          2D game: jump crates and duck bars; the flow is the autopilot,
                          or wire the Key blocks and play with Space and ↓. Its Station is
                          its own web page (Python)
  animation-python/       Step flow: move, rotate and scale a 3D actor in sequence,
                          Scratch style; the active block lights up (Python)
  parts-cpp/              no simulation: count bolts, nuts and washers in a photo with an
                          OpenCV pipeline on the Flow page (C++)
  chain-python/           a structure built on the Station as a graph (masses, springs,
                          dampers), tested from the Flow page as stories (Python)
  supply-python/          no physics: a supply network on the Station as a graph, the
                          ordering policy on the Flow page (Python)
docs/                     plan, and the API reference (docs/api, reStructuredText)
```

The UI has no domain code. The backend sends a block catalog, a scene and a starter
graph; the UI builds the palette, node forms, canvas and 3D view from them (see
`protocol/README.md`). The eight example backends differ in domain, language, run style
(stepped in time, evaluated once, or interactive), flow type (Signal or Step flow) and
Station view (3D or 2D scene, a graph, the backend's own web page), run on the same UI
unchanged, and all pass the same domain-neutral smoke test and their golden runs.

## Run the proof of concept

```sh
pip install websockets
npm install
npm run backend:mass-spring   # ws://localhost:8765
npm run backend:cart-pole     # ws://localhost:8766
npm run backend:parametric    # ws://localhost:8767
npm run backend:runner        # ws://localhost:8768
npm run backend:animation     # ws://localhost:8769
npm run backend:parts         # ws://localhost:8770 (needs OpenCV)
npm run backend:chain         # ws://localhost:8771
npm run backend:supply        # ws://localhost:8772
npm run dev                   # http://localhost:3047
```

Or manage backends, toolkits, and the web app with the unified `paperboat` CLI:

```sh
./bin/paperboat run cart-pole       # builds and runs cart-pole backend
./bin/paperboat list                # lists all active backends
./bin/paperboat app start           # starts web app on port 3047
./bin/paperboat examples start      # launches all example backends (ports 8765-8773)
./bin/paperboat examples status     # checks status of example backends
./bin/paperboat toolkit list        # lists available and installed toolkits
```

The UI opens on the mass-spring backend. To switch, type another backend's URL in the
header and press **Connect**: the palette, canvas and scene change to that domain, and
**Run** runs its starter graph.

Check any backend against the protocol with `npm run smoke -- 8767` (any port), and
every example's starter result with `npm run golden`.
