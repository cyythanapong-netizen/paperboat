# toolkit

Reusable C++ blocks with no domain code, grouped in packages. A domain doesn't
re-implement a PID or a sine: its **descriptor** lists the templates it wants, renames
or re-defaults them, and adds the blocks only it can provide (the sources and probes on
its plant).

## Templates

| Package   | Templates |
| --------- | --------- |
| `base`    | What every domain needs, in every flow type. Values of each datatype: `constant` (Number, a float), `text` (Text), `boolean` (Boolean: 1 or 0) · input: `key` (1 while its `key` param's key is held, from the UI's `input` messages) · logic, as magic blocks (two forms, picked by the `form` param: `signal` computes `y`, `step` branches `in` → `yes`/`no`): `if` (signal: `a` while `cond` holds, else `b`), `and`, `or`, `not`, `compare` (`op`) |
| `signal`  | Signals f(t): t is the block's input `t` when wired (a step's `progress`), else the run clock. C++: `clock`, and the abstract parent `generator` (f is an expression). TOML definitions on it (`src/signal/blocks.toml`): `sine`, `step`, `ramp`, `square`, `ease` (shape a 0–1 progress; `shape`: in-out, in, out, linear), `function` (f(t): type f on the block, over t, a, b, c) |
| `math`    | Pointwise arithmetic on signals: `gain`, `sum`, `threshold`, `saturation` |
| `dashboard` | Monitors, shown on the Dashboard as instruments: `graph` (plots its input over time), `display` (its latest value as a number or text) |
| `control` | Dynamics that run on a clock in a Signal flow: `pid`, and without direct feedthrough (a feedback loop may pass through them) `integrator`, `lag`, `delay` (`steps`, `initial`) |
| `opencv`  | Optional, built when OpenCV is found (`paperboat::toolkit_opencv`, `register_opencv`). Image filters on `image` ports: `gray`, `blur`, `median`, `threshold`, `adaptive_threshold`, `hsv_range`, `canny`, `erode`, `dilate`, `open`, `close`, `invert`, `distance` · monitor: `view` (an image instrument) · station: `image` (a picture from a `file` prop; its linked block outputs it) |
| `step`    | The path of a Step flow, with step ports. Hats: `start` (once), `when` (each time signal `cond` rises, e.g. a key; a path alongside the others). Then `wait` (`seconds`), `repeat` (leaves by `loop` `times` times, then `done`), `stop` (ends the whole run). Branch with a `base` logic block in its step form |

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
a clock in `control`, image filters in `opencv`. A descriptor imports the packages it
wants, so a domain's palette shows only what makes sense there.

The control package also has `rk4_step` and `euler_step` in
`paperboat/toolkit/control/simulator.hpp`, for domain plants to integrate their own
equations of motion.

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
import = ["base", "signal", "math", "control", "dashboard"]

# 1. A toolkit template, used as is: type "pid", label "PID", kp=1 ki=0 kd=0
[[blocks]]
use = "control/pid"

# ...or customised: a new type id and label, new param defaults
[[blocks]]
use = "control/pid"
type = "pid-theta"
label = "PID θ"
defaults = { kp = 40, kd = 8 }

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
(`examples/backends/<domain>-<language>/descriptor.toml`).

Top level: `domain` (name, also the palette group of its own blocks), `import` (list of
packages: their blocks and station objects) and `[[objects]]` (station object types: a
`use = "opencv/image"` with `defaults`, or the domain's own with `type`, `role`,
`props`, `links` and `block = { kind, inputs, outputs }` for its linked block).
Domain objects are implemented by the backend (`StationObject`); toolkit ones by the
toolkit.

**Starters** (both optional, checked against the catalog when loaded):

```toml
[station]                       # station.load: the Station page's opening objects
objects = [
  { id = "cart-pole-1", type = "cart-pole", props = { theta0 = 0.15 } },
]

[graph]                         # graph.load: the Flow page's opening graph; empty if absent
nodes = [
  { id = "pid-theta", type = "pid", params = { kp = 40, kd = 8 }, pos = [260, 0] },
  { id = "cart-pole-1", type = "cart-pole", pos = [0, 120] },
]
edges = [
  { from = ["cart-pole-1", "theta"], to = ["pid-theta", "e"] },
]
```

`Catalog::graph` and `Catalog::station` hold them as the protocol's JSON. The catalog lists the domain's blocks first, then each imported package in
order; every entry carries its `group`. Python example backends load the same files
through `python/paperboat/toolkit.py`, which mirrors `base` and `step`.

| Field      | Template entry | Domain entry | Meaning |
| ---------- | :---: | :---: | --- |
| `use`      | required | — | template name, `<package>/<block>` |
| `type`     | optional | required | unique id in the catalog (template default: last part of `use`) |
| `label`    | optional | optional | palette name (default: template label, or `type`) |
| `defaults` | optional | — | `{param: number}` overriding template defaults |
| `config`   | optional | — | `{key: string}` fixed at load time, not shown in the UI |
| `kind`     | — | required | `source`, `probe`, `function` or `monitor` |
| `inputs` / `outputs` | — | optional | port ids, or `{ id = "x", type = "position" }` tables |
| `params`   | — | optional | `{ param = default }`, kept in the order written |

Mistakes fail loudly with their location, e.g.
`blocks[2] (control/pid): control/pid has no param "kq"`, and unknown fields are
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

1. Write the block in `src/<package>/` as a small struct deriving from `Block` (or
   `Step` for a Step flow's path); keep its state in the struct.
2. Register it in that package's `register_<package>()` with a `Template`
   (`name`, `def`, `config`, and `make` or `make_step`).
3. Add tests in `tests/`.
