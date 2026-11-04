# Paperboat design system

How the web app looks and why. The look is **flat mono, like a well-made instrument**:
grey panels with hairlines, small lowercase labels, black displays for values that change,
and one colour that means "live". It takes its spirit from hardware such as Teenage
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
   is selected is shown by position and line: a switch's knob sits on the chosen side
   and that name is bold ink; the current page has a bar beside it; a selected block has
   a 2px outline set off from it; a selected wire is thicker.
4. **State in words.** A block's head says what it is (`fn`, `source`, `monitor`,
   `step`, `station`) or what is happening to it (`running`, `failed`, `missing`).
5. **Displays hold what changes.** Values that move (the backend's address, run length
   and speed, instruments, the Console) sit in black displays set into the panel.
6. **Room to breathe.** 14px between sections and around the app, 16px inside them.
7. **Simple to enter.** A first-time user sees one page (Navigator), a Run key and the
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
│ ⛵ paperboat  [ws://… engine] connect  ▶ run  for [10] s [1×] ■ cancel    │
│                                   navigator ◯━ commander   theme ━◯━       │
├────────────┬─────────────────────────────────────────────────────────────┤
│ [1] station│ [2] flow                                         6 blocks   │
│▌[2] flow   │ group [group][story]  signal [+ sine]…  math [+ gain]…       │
│ [3] dashb… │                                                             │
│ [4] console│        ┌ pid ───── fn ┐     ┌ sum ───── fn ┐                │
│            │        │ kp      40   │ ─── │ a            │                │
└────────────┴─────────────────────────────────────────────────────────────┘
```

Below 860px the rail moves to the bottom (the current page underlined), the bar wraps,
and Commander stacks its pages in one scrolling column. Below 1280px the bar drops its
small labels.

## Tokens

Three layers. Components use semantic and component tokens only: never a primitive,
never a raw hex.

**Primitives**: greys (`--grey-50` to `--grey-900`) for light, `--night-*` for dark,
`--black` and `--screen-*` for displays, `--orange` (placeholder) and `--red`; fonts
`--font-sans` (Inter Tight) and `--font-mono` (IBM Plex Mono), both self-hosted; sizes
10.5 / 11.5 / 13 / 15 / 30 px; a 4px spacing grid.

**Semantic**: `--surface-ground | panel | raised | screen | float`, `--float-blur`, `--text`,
`--text-muted`, `--text-on-screen(-muted)`, `--line` (dividers), `--line-strong` (keys,
blocks, fields), `--line-ink` (wires, switches, selection), `--dots`, `--signal`,
`--danger`, `--focus`, `--scene-bg`, `--scene-grid`, radii, `--motion`. Dark redefines these
(`html[data-theme="dark"]`, or the system's dark preference when the user chose auto).

**Component**: `--bar-height`, `--rail-width`, `--panel-head-height`, `--gutter`,
`--panel-pad`, `--select-outline`, `--select-offset`.

## Components

| Component | Rule |
| --- | --- |
| **Label** | 10.5px monospace, lowercase, grey. Names things; never shows selection |
| **Key** (every button) | Flat, 1px outline, lowercase, 5px radius. Hover inks the outline. Run has an ink outline and bold label |
| **Switch** | An outlined track with a small ink knob on the chosen option; the chosen name bold ink, the rest grey. Two options sit either side; three sit below. Click a name, click the track to step, or use the arrow keys |
| **Display** | Black, 5px radius; light monospace text; the signal colour for traces and live values |
| **Bar** | The wordmark, the backend (a display with the address and engine name; connect), the run (run, length, speed, cancel, status in words), the view and theme switches |
| **Rail** | Numbered pages; the current one has a 2px ink bar beside it, a bold name and an inked number |
| **Panel** | Grey, 1px edge, 6px radius; a head with its number, name and a short count on the right |
| **Block** | Raised grey, 1px outline; head with the name and a word tag. Step blocks are outlined in ink; linked blocks dashed; missing ones dashed red; failed ones outlined red. Params are underlined values |
| **Wire** | Ink, 1.2px; step wires 2px; image wires dashed; selected wires 3px |
| **Group / story** | A visual group is a dashed frame; a story a solid ink frame |
| **Instrument** | A display with its name (and latest value) underneath; readouts show a 30px monospace number |
| **Console** | A display; numbered lines; warnings in the signal colour, errors red; a block's id is a key that shows it on the Flow page |
| **Floating panel** | The only glass: what floats over live content (the station's properties). One token (`--float-blur: none`) retires it |

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
- Still open (#48, #33): the signal colour, the palette (search, drag to place), the
  Monitor mode, the Dashboard grid (#49).
