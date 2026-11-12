# toolkit

Reusable C++ blocks with no domain code, grouped in packages. A domain doesn't
re-implement a PID or a sine: its **descriptor** lists the templates it wants, renames
or re-defaults them, and adds the blocks only it can provide (the sources and probes on
its domain state).

## Templates

| Package   | Templates |
| --------- | --------- |
| `base`    | What every domain needs, in every flow type. Values of each datatype: `constant` (Number, a float), `text` (Text), `boolean` (Boolean: 1 or 0), `variable` (a project variable's value by its `name`; live if the variable is `tunable`) · input: `key` (1 while its `key` param's key is held, from the UI's `input` messages) · logic, as magic blocks (two forms, picked by the `form` param: `signal` computes `y`, `step` branches `in` → `yes`/`no`): `if` (signal: `a` while `cond` holds, else `b`), `and`, `or`, `not`, `compare` (`op`) |
| `signal`  | Signals f(t): t is the block's input `t` when wired (a step's `progress`), else the run clock. C++: `clock`, `fft` (the amplitude spectrum of the last N samples via pocketfft, and its strongest frequency), and the abstract parent `generator` (f is an expression). TOML definitions on it (`src/signal/blocks.toml`): `sine`, `step`, `ramp`, `square`, `ease` (shape a 0–1 progress; `shape`: in-out, in, out, linear), `function` (f(t): type f on the block; its free names are its inputs, with t as the clock when unwired) |
| `math`    | Pointwise arithmetic on signals: `gain`, `sum`, `threshold`, `saturation`, and special functions from Boost.Math: `erf`, `gamma` (gamma/log-gamma/digamma), `bessel` (J/Y/I/K, of any order), `distribution` (normal/uniform/exponential pdf, cdf or quantile). Matrices on `matrix` ports: `matrix_constant` (MATLAB-style rows), `transpose`, `inverse` and `determinant` (Eigen), `multiply`, `solve`, `scale`, `identity`, `size` |
| `dashboard` | Monitors, shown on the Dashboard as instruments: `graph` (plots its input over time), `display` (its latest value as a number or text), `scope`, `lamp` (on while its input passes `threshold` by `op`; `latch`; outputs `y`), `view3d` (the Station's 3D scene, objects and meshes), and the `switch` control |
| `step`    | The path of a Step flow (`flows = ["step"]`), with step ports. Hats: `start` (once), `when` (each time signal `cond` rises, e.g. a key; a path alongside the others). Then `wait` (`seconds`), `repeat` (leaves by `loop` `times` times, then `done`), `stop` (ends the whole run). Branch with a `base` logic block in its step form |
| `optimization` | External optional toolkit (`paperboat-toolkit-optimization`, installed into `engine/toolkit/optimization`; `paperboat::toolkit_optimization`, `register_optimization`). Step blocks from a JSON file: `qp` (min ½xᵀPx + qᵀx), `lp` (min cᵀx) and `nlp` (min f(x), Ipopt when built), with `l ≤ Ax ≤ u`; step `in`/`out` and outputs `x`, `objective`, `iterations`, `status` |
| `control` (external) | External optional toolkit (`paperboat-toolkit-control`, installed into `engine/toolkit/control`; `paperboat::toolkit_control`, `register_control`). Clock dynamics (`flows = ["signal", "step"]`): `pid`, and without direct feedthrough (a feedback loop may pass through them) `integrator`, `lag`, `delay` (`steps`, `initial`). Links the optimization `Workspace` for per-step Signal blocks: `mpc` (condensed QP from `A`, `B`, `Q`, `R`, `N`, `u` bounds), `lp_step` (LP every step), `nmpc` (NLP every step, needs Ipopt); outputs `u`, `status`, `iterations`, `solve_time` |
| `opencv`  | External optional toolkit (`paperboat-toolkit-opencv`, installed into `engine/toolkit/opencv`; `paperboat::toolkit_opencv`, `register_opencv`). Image stages on `image` ports: geometry (`resize`, `crop`, `flip`, `rotate`), colour/tone (`gray`, `convert`, `normalize`, `equalize`, `clahe`), filters/edges (`blur`, `median`, `threshold`, `adaptive_threshold`, `hsv_range`, `canny`, `sobel`, `laplacian`, `erode`, `dilate`, `open`, `close`, `invert`, `distance`), algebra (`bitwise`, `blend`, `absdiff`, `background`), analysis (`stats`, `centroid`, `contours`, `hough_lines`, `hough_circles`, `template_match`), annotation/output (`draw`, `save`) · monitor: `view` (an image instrument) · station: `image` (a picture from a `file` prop; its linked block outputs it) · examples: `ocv-01`…`ocv-03` under `examples/` |

## Package layout

Each built-in package is one folder, `src/<package>/`; each block is a folder in it, with
everything of the block in that folder:

```
src/math/
  toolkit.toml            name = "math"
  arithmetic_shared.hpp   a helper a few blocks share
  gain/
    gain.cpp              one block, one .cpp: defines add_gain(Registry&) in paperboat::toolkit::math
    icon.hpp              its look on the plate: a view.icon
    web/                  its UI, if any (instrument, CSS, tests, package.json)
    design.html           its design mockups and notes
```

The manifest's `name` is its namespace and palette group. CMake finds each block's
`add_<folder name>` from the folders (`<block>/<block>.cpp`), compiles the sources and generates
the registry dispatcher. A package can embed TOML definitions on a C++ parent with
`definitions = "blocks.toml"` (see `signal`; registered after the folders); an explicit
`register = [...]` replaces the folder rule. There is no central list of packages, block
descriptions, glyphs or UI views to update, and deleting the folder leaves everything else
working.

Descriptions and view defaults belong to the block definition in its source file. A
descriptor that defines a block on a parent inherits its description/view and can override
the description or individual view keys. This keeps a block's behavior, palette entry and
look together.

**Flow types (#9).** A block works in the flow types `BlockDef::flows` names (`signal`,
`step`, `state`, `model`); an empty list means every one. A package's `toolkit.toml` sets the
default for its blocks with `flows = ["signal"]` (one line), a block in code may set its own
(a `Step` defined with `define_block` defaults to `["step"]`), and a descriptor entry overrides
both. The catalog carries the list as `flows` (absent: every flow type), the Flow page's palette
offers a block only on a sheet of a type it works in, and the engine refuses it on another.

| Package | Flow types |
| ------- | ---------- |
| `base`, `signal`, `math`, `dashboard`, `string` | every one (a value, a wave, arithmetic, a monitor work anywhere; in a Step flow they run once per clock step) |
| `control` (external) | `signal` and `step` (a clock's dynamics, or signal blocks on a Step flow) |
| `step` | `step` (the path itself) |
| a domain's own | every one, unless the block says (`flows` in C++ or Python) or the descriptor does; e.g. an electrical package: `model` |

An optional toolkit with extra dependencies (such as OpenCV) lives in
`engine/toolkit/<package>/`, with its own `toolkit.toml` and `CMakeLists.txt`. The engine
discovers that folder; its CMake file decides whether the dependency is available and which
target to build. A backend opts into an optional package by linking its target and adding
its registrar to the `Registry`.

The root `toolkits.toml` is a catalog, not an installer: its `repo` is a suggested source.
A backend's `toolkits = [...]` declares what it needs; `paperboat check` verifies the local
install and never downloads. Install a toolkit from any repository or local folder with
`paperboat toolkit install <name> --repo <url>` (or `--path <dir>`).

Step templates make a `Step` (`enter`, then `tick` once per clock step until it names the
step output to leave by; a hat is ticked while idle and has `may_start()`) instead of a `Block`: use `CatalogEntry::make_step()` for them
and `make()` for the rest. A domain's own step blocks declare step ports in its
descriptor: `inputs = [{ id = "in", type = "step" }]`.

**Flow templates and station templates.** A package can offer both. A flow template is
a block (`Template`). A station template (`StationTemplate`) is a station object type: an
`ObjectDef` (props, links, the linked block it spawns) and a factory for a
`StationObject` (`read()` the linked block's outputs, `step()` under its inputs,
`entities()` to draw it). Its properties are edited on the Station page only (ISA-88:
equipment parameters live in the physical model, not in the procedure).

**Where a block goes.** Paperboat is not Simulink: `base` stays small, holding only what
any domain needs (a value, a key press). Everything else goes in the package of what it
is about: waves in `signal`, arithmetic in `math`, monitors in `dashboard`, a PID that needs
a clock in the optional `control` toolkit, image filters in `opencv`. A descriptor imports the packages it
wants, so a domain's palette shows only what makes sense there.

The optional control package also has `rk4_step` and `euler_step` in
`paperboat/toolkit/control/simulator.hpp`, for domains to integrate their own
equations of motion once it is installed.

List them with ports and defaults:

```sh
build/dev/tools/paperboat-catalog --templates
```

## Descriptors

A domain's `descriptor.toml` is a [TOML](https://toml.io) file: a `domain` name and one
`[[blocks]]` table per palette entry. Each entry is one of two kinds.

```toml
domain = "cart-pole"

# 0. Whole packages: every template of these joins the palette under its default type.
#    Entries below with the same type replace the imported one.
import = ["base", "signal", "math", "dashboard"]

# 1. A toolkit template, used as is: type "pid", label "PID", kp=1 ki=0 kd=0
[[blocks]]
use = "math/gain"

# ...or customised: a new type id and label, new param defaults
[[blocks]]
use = "math/gain"
type = "gain-theta"
label = "Gain θ"
defaults = { k = 40 }

[[blocks]]
use = "base/key"
type = "key-left"
label = "Key (←)"
defaults = { key = "ArrowLeft" }   # a key param: text; the user can still change it

# 2. A block the domain implements itself: no `use`, needs a `kind`
[[blocks]]
type = "motor"
label = "Motor · force on cart"
kind = "source"
inputs = ["force"]
params = { max_force = 50 }

[[blocks]]
type = "encoder"
kind = "probe"
outputs = ["theta", "omega", "x", "v"]
```

A descriptor lives next to the backend that serves it
(`<domain>-<language>/descriptor.toml`, as in the paperboat-examples repository).

Top level: `domain` (name, also the palette group of its own blocks), `import` (list of
packages: their blocks and station objects) and `[[objects]]` (station object types: a
`use = "opencv/image"` with `defaults`, or the domain's own with `type`, `role`,
`props`, `links` and `block = { kind, inputs, outputs }` for its linked block).
Domain objects are implemented by the backend (`StationObject`); toolkit ones by the
toolkit.

**Flow types offered** (optional): top-level `flows = ["signal", "step"]` says what a sheet
may be on this domain, sent in `hello.flows`. Without it: a Signal flow, and every flow type a
block of the catalog names (a domain importing `step` offers a Step flow too).

**Starters** (optional, checked against the catalog when loaded):

```toml
[station]                       # station.load: the Station page's opening objects
objects = [
  { id = "cart-pole-1", type = "cart-pole", props = { theta0 = 0.15 } },
]

[graph]                         # graph.load: the Flow page's opening graph; empty if absent
flow = "signal"                 # optional: the sheet's flow type; each block must work in it
nodes = [
  { id = "pid-theta", type = "pid", params = { kp = 40, kd = 8 }, pos = [260, 0] },
  { id = "cart-pole-1", type = "cart-pole", pos = [0, 120] },
]
edges = [
  { from = ["cart-pole-1", "theta"], to = ["pid-theta", "e"] },
]
```

A third optional table, `[dashboard]`, is the default layout of the Dashboard's grid. Its
shape alone is checked (blocks are not: a layout may name monitors the user has not added
yet, and the UI ignores unknown ones):

```toml
[dashboard]                     # sent in the catalog; the user's own arrangement wins
columns = 6                     # 1 to 12; fewer on a narrow screen
instruments = [
  { block = "graph-theta", size = "large", at = [0, 0] },   # size: compact, normal or large
  { block = "speed", size = "compact" },                    # at: [column, row], from 0; optional
]
tabs = ["Run", "Tuning"]        # optional pages; an instrument or decoration names its page with tab = "Run"
decorations = [                 # optional, kinds: section, hline, vline, label, console, station
  { kind = "label", text = "Speed {speed:.1f} m/s", at = [0, 0], span = [4, 1], style = "heading" },
  { kind = "section", text = "Stores", at = [0, 1], span = [4, 2] },
  { kind = "hline", at = [0, 3], span = 6 },                # a separator: span is a length in cells
  { kind = "console", at = [0, 4], span = [6, 3] },         # the Console's lines in a frame
  { kind = "station", at = [0, 7], span = [6, 3] },         # a read-only view of the Station
]
```

Text in a label or section is a formatted string: `{block}` is that monitor's latest value,
`{block:.2f}` with that many decimals, `{{` and `}}` are braces.

`Catalog::graph` and `Catalog::station` hold the starters as the protocol's JSON, `Catalog::dashboard` the layout. The catalog lists the domain's blocks first, then each imported package in
order; every entry carries its `group`. Python example backends load the same files
through `python/paperboat/toolkit.py`, which mirrors `base` and `step`.

| Field      | Template entry | Domain entry | Meaning |
| ---------- | :---: | :---: | --- |
| `use`      | required | — | template name, `<package>/<block>` |
| `type`     | optional | required | unique id in the catalog (template default: last part of `use`) |
| `label`    | optional | optional | palette name (default: template label, or `type`) |
| `description` | optional | optional | one line, shown on its card in the block browser (default: the template's) |
| `view`     | optional | — | how the UI draws it: `{ compact = true, glyph = "gain", value = "{k}" }`; any key, and a key not given is the template's (see below) |
| `defaults` | optional | — | `{param: number}` overriding template defaults |
| `flows`    | optional | — | the flow types it works in, `["signal", "step"]` (default: the template's, see Flow types) |
| `config`   | optional | — | `{key: string}` fixed at load time, not shown in the UI |
| `kind`     | — | required | `source`, `probe`, `function` or `monitor` |
| `inputs` / `outputs` | — | optional | port ids, or `{ id = "x", type = "position" }` tables |
| `params`   | — | optional | `{ param = default }`, kept in the order written |

**View.** A block's `view` says how the UI draws it: `compact` (a small plate: a glyph, bare
port dots, a caption with the name and value; unset: the normal card), `glyph` (a curve
name: `gain`, `plus`, `step`, `clamp`, `const`; or a few characters drawn as text) and
`value` (an expression of the params, `{k}` or `{min}…{max}`, shown in the caption). It is a
hint, sent as `view` in the catalog. A definition inherits its template's view and
overrides single keys: `view = { glyph = "plus" }` on `math/gain` keeps `compact` and
`value`; `view = { compact = false }` draws it as a card. The simple `math` blocks (gain, sum,
threshold, saturation) and the `base` constant and logic blocks are compact by default
(`BlockDef::view` in each package's source). A Python block sets a `view` class attribute (a
dict of the same keys).

Mistakes fail loudly with their location, e.g.
`blocks[2] (math/gain): math/gain has no param "kq"`, and unknown fields are
rejected so a typo like `lable` doesn't pass silently. TOML syntax errors give the line
(`not valid TOML: line 12: …`).

**`params` vs `config`:** params are what the user edits on a block in the UI: numbers,
or `key` params (which key a Key block reads; `defaults` sets the starting key). Config
is text the domain author fixes and the user never sees; none of the built-in templates
needs one now.

Load one from C++ and turn it into the protocol's `catalog` message:

```cpp
#include <paperboat/toolkit/descriptor.hpp>
namespace tk = paperboat::toolkit;

// `registry`: the toolkit (tk::builtins()) plus the domain's own blocks and station objects.
auto catalog = tk::load_descriptor_file("descriptor.toml", registry);
send(tk::catalog_message(catalog));                    // what the UI's palette shows
auto pid = catalog.find("pid")->make({{"kp", 40}});    // an instance for a graph.run
```

Or from the shell, for a descriptor that uses only the toolkit:
`build/dev/tools/paperboat-catalog descriptor.toml` (`--starters` also prints the starter
graph and station).

## Adding a template

1. Write the block in `src/<package>/<block>/<block>.cpp` as a small struct deriving from
   `Block` (or `Step` for a Step flow's path); keep its state in the struct. One block per folder.
2. In the same file define its `BlockDef` (including description and optional view; a glyph
   goes in `<block>/icon.hpp`) and register it from `void add_<block>(Registry&)` in the
   package namespace. The folder name is the registration: no manifest, registry or list to
   edit. A helper two blocks share goes in a `<topic>_shared.hpp` in the package folder.
3. Say whether a run that evaluates once may reuse its outputs from an earlier run
   (`BlockDef::cacheable`): `true` for a pure function of its params, text, inputs and the
   variables it reads through `Context::variable` (text, format, conversions); `false` for a
   block that reads keys or the clock, draws at random, or acts on anything else (key,
   random_number, now). Unset, it follows `Block::stateless()` (the `Stateless` math blocks are
   reused). The core keeps the results (`engine/core/include/paperboat/core/cache.hpp`).
4. Add C++ tests in `engine/tests/`. If it is a new built-in package, create
   `src/<package>/toolkit.toml` with its `name`; the built-in registry and build discover it
   without edits to `registry.cpp` or the top-level toolkit source list.
