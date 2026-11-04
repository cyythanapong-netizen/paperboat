# Paperboat design system

How the web app looks and why. The look is **flat mono, like a well-made instrument**:
grey panels with hairlines, clear sentence-case labels, recessed grey fields for what you
type, black displays for output, and one colour that means "live". It takes its spirit from hardware such as Teenage
Engineering's: honest, precise and quiet, with a little character in the type and the
displays. Flat: no bevels, no indicator lights, no shadows.

Tokens live in [`src/tokens.css`](src/tokens.css), the rules in [`src/styles.css`](src/styles.css),
and the frame (view modes, rail, panels, switches) in [`src/Shell.tsx`](src/Shell.tsx).
The direction and its history are in issue #48.

## Principles

1. **Mono first.** Ink on grey. Hierarchy comes from weight, size, spacing and lines,
   not colour.
2. **One colour, one meaning.** The signal colour (orange for now, a placeholder until
   #48 settles it) means live or look here: a run in progress, a block that is running,
   a trace, a warning. Red means failed. Nothing else is coloured.
3. **Selection is never a fill.** A filled shape doesn't say whether it is on or off. What
   is selected is shown by line and weight: a chosen option sits in brackets in bold ink
   (`[ navigator ]  commander`); the current page is boxed in ink and stands out on panel
   grey from the greyed rail; a selected block has a 2px outline set off from it; a
   selected wire is thicker.
4. **State in words.** A block's head says what it is (`fn`, `source`, `monitor`,
   `step`, `station`) or what is happening to it (`running`, `failed`, `missing`).
5. **Displays are for output; wells are for input.** What the run shows (instruments, the
   Console) sits in black displays. What you type (the backend's address, run length,
   speed) sits in recessed grey wells: present, never loud.
6. **Readable first.** Body 13px, labels 12px in a dark grey, values 12.5px monospace.
7. **Room to breathe.** 14px between sections and around the app, 16px inside them.
8. **Simple to enter.** A first-time user sees one page (Navigator), a Run key and the
   flow the backend opened with. Nothing needs setting up before the first run.

## Themes

Light (grey panels) and dark (near-black panels), built on the same semantic tokens. The
theme follows the system unless the user picks one with the theme switch (auto, light,
dark); the choice is kept in this browser. The 3D station reads its background and grid
from the tokens and is rebuilt when the theme changes. Displays are black in both themes.

## View modes

| Mode | What it shows | When |
| --- | --- | --- |
| **Navigator** (default) | One page at a time, full size; the rail picks it (Alt+1 to Alt+4). The Console's rail item shows a count of errors | Building one thing: wiring a flow, setting up the station, reading a run |
| **Commander** | Every page at once: Station and Flow on top, the Dashboard and Console below. Each head has **open**, which shows that page alone in Navigator | Running and watching |
| **Monitor** (planned) | A finished program's Dashboard, Station and Console collapsible | Using a rig someone built |

Switch with the view switch on the bar, or Alt+M. Every page stays mounted in both modes,
so switching never loses a canvas, a scene or a run; a canvas fits itself again when its
size changes.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ⛵ paperboat  ░ws://… engine░ Connect  ▶ Run  for ░10░ s ░1×░  ■ Cancel   │
│                          [ navigator ]  commander  │  [ auto ]  light  dark │
├────────────┬─────────────────────────────────────────────────────────────┤
│ 1 Station  │ 2 Flow                                            6 blocks  │
│┏━━━━━━━━━━┓│ Group [Group][Story]  Signal [+ Sine]…  Math [+ Gain]…       │
│┃ 2 Flow   ┃│                                                             │
│┗━━━━━━━━━━┛│        ┌ PID ───── fn ┐     ┌ Sum ───── fn ┐                │
│ 3 Dashb…   │        │ kp      40   │ ─── │ a            │                │
└────────────┴─────────────────────────────────────────────────────────────┘
```

Below 860px the rail moves to the bottom (numbers only, the current page boxed with its
name), the bar wraps, and Commander stacks its pages in one scrolling column. Below
1560px the bar drops its small labels and the engine's name.

## Tokens

Three layers. Components use semantic and component tokens only: never a primitive,
never a raw hex.

**Primitives**: greys (`--grey-50` to `--grey-900`) for light, `--night-*` for dark,
`--black` and `--screen-*` for displays, `--orange` (placeholder) and `--red`; fonts
`--font-sans` (Geist) and `--font-mono` (Geist Mono), both self-hosted; sizes
12 / 12.5 / 13 / 15 / 30 px; a 4px spacing grid.

**Semantic**: `--surface-ground | panel | raised | sunken | well | screen | float`, `--well-shadow`, `--float-blur`, `--text`,
`--text-muted`, `--text-on-screen(-muted)`, `--line` (dividers), `--line-strong` (keys,
blocks, fields), `--line-ink` (wires, switches, selection), `--dots`, `--signal`,
`--danger`, `--focus`, `--scene-bg`, `--scene-grid`, radii, `--motion`. Dark redefines these
(`html[data-theme="dark"]`, or the system's dark preference when the user chose auto).

**Component**: `--bar-height`, `--rail-width`, `--panel-head-height`, `--gutter`,
`--panel-pad`, `--select-outline`, `--select-offset`.

## Components

| Component | Rule |
| --- | --- |
| **Label** | 12px sans, sentence case, dark grey. Names things; never shows selection |
| **Key** (every button) | Flat, 1px outline, 5px radius. Hover inks the outline. Run has an ink outline and bold label |
| **Choice** (view, theme) | Options in monospace; the chosen one in brackets and bold ink, the rest grey. The brackets are reserved for every option, so nothing shifts. Click one, or use the arrow keys |
| **Remove** (round ×) | 16px or 18px circle, grid-centred, with the × drawn as two SVG strokes (`RemoveIcon`) in the button's colour. Never a text glyph: a glyph centres on its font's line box and drifts with the font |
| **Minimap** | Bottom-right of the Flow canvas, Navigator mode on the Flow page only (never in Commander). A sunken grey well with a 1px outline: blocks in grey, groups as outlines, running in the signal colour, failed red; the view is the clear window in the mask. Click or drag to move the view, scroll to zoom. Kinds are not coloured |
| **Well** | A recessed grey field for input (address, run length, speed); ink text, no spinners |
| **Display** | Black, 5px radius; light monospace text; the signal colour for traces and live values. For output only |
| **Bar** | The wordmark, the backend (a well with the address and engine name; Connect), the run (Run, length, speed, Cancel, status in words), view and theme, divided by a hairline |
| **Rail** | Numbered pages on a sunken grey rail; the current one is boxed in ink on panel grey, its name bold |
| **Panel** | Grey, 1px edge, 6px radius; a head with its number, name and a short count on the right |
| **Block** | Raised grey, 1px outline; head with the name and a word tag. Step blocks are outlined in ink; linked blocks dashed; missing ones dashed red; failed ones outlined red. Params are underlined values |
| **Wire** | Ink, 1.2px; step wires 2px; image wires dashed; selected wires 3px |
| **Group / story** | A visual group is a dashed frame; a story a solid ink frame |
| **Instrument** | A display with its name (and latest value) underneath; readouts show a 30px monospace number. Four sizes on a grid of equal cells (116px tall, 12px apart), picked S, M, L, XL: **compact** 1×1 (a readout), **normal** 2×1 (a plot to read at a glance), **large** 2×2 (a plot or picture to study), **xlarge** 3×3 (the one to fill the page with); a kind's default is display compact, graph normal, image large. Before a run sends anything it is a placeholder: same frame, greyed, dashed edge, "–" |
| **Arrange** | The Dashboard head's key. While on: a dashed outline round each instrument, faint dashed cells under the grid, a size picker (C N L in brackets, full names on hover) top right of each, a solid outline where a drag would land. Drag to a cell, or focus and use the arrows. Reset layout returns to the backend's default |
| **Console** | A display; numbered lines; warnings in the signal colour, errors red; a block's id is a key that shows it on the Flow page |
| **Card** | A block or station object in the browser: name, kind in words, one-line description, ports on a well (in → out; step ports square), params with defaults, how many are already used, and Add. Draggable |
| **Collection window** | The browser, floating over the page from its Add key: search, a kind choice in brackets, packages on the left, cards in sections. Moved by its head and remembered; Escape closes. While a card is dragged it fades and lets the pointer through |
| **Quick add** | A search floating at the cursor (`/` over the canvas, or a double-click on empty canvas): the best matches, arrows, Enter adds at that spot |
| **Drop hint** | While a card is dragged over the Flow: a dashed outline where it lands, a thickened wire it would be inserted into, or a dashed line to the port it would connect to; with a label in words ("Insert Lag: pid.u → u, y → sum.a") |
| **Port (while a wire is dragged)** | A 24px hit area round the 8px dot. A drop within 40px of a port that accepts the wire joins it (a wire's end is grabbed within 20px; both in `SNAP`, `wireSnap.tsx`). The port it would join is enlarged, ringed, in the signal colour, with its name bold; ports that can't take the wire (wrong type, same block) dim to 30%. The nearest port that accepts wins, not the nearest port |
| **Floating panel** | The only glass: what floats over live content (the station's properties). One token (`--float-blur: none`) retires it |
| **View cube** | A 96px cube in the 3D Station's top-right corner (hidden in 2D views): a mono label on each face, ink edges, turning with the camera. Hover marks the third of the cube a click would pick (face, edge or corner) in the signal colour; click eases the camera there (260ms, none with reduced motion); drag orbits; a Home key under it restores the opening view |

Focus is a 2px ink ring. Disabled keys are grey and faded.

## Motion

Only in answer to an action: a switch's knob slides (150ms). Nothing moves on its own; with
reduced motion nothing animates.

## Accessibility

Text is at least 4.5:1 on its surface in both themes. Every control works from the
keyboard with a visible focus ring; the switches are radio groups, the rail a `nav`, each
page a labelled `section`. Colour is never the only signal: states are also words,
positions and lines.

## Doing more

- A new component uses existing tokens; a new value goes in as a primitive, then a
  semantic role, then is used through the role.
- A backend's own look belongs in a web station (`[station] view = "web"`).
- Still open (#33): the signal colour, the Monitor mode.
