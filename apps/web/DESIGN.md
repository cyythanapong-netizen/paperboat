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
   (`[ navigator ]  commander`); the current page sits in the same ink brackets on the
   rail; a selected block has a 2px outline set off from it; a selected wire is thicker.
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
theme follows the system unless the user picks one in View > Theme (auto, light,
dark); the choice is kept in this browser. The 3D station reads its background and grid
from the tokens and is rebuilt when the theme changes. Displays are black in both themes.

## View modes

| Mode | What it shows | When |
| --- | --- | --- |
| **Navigator** (default) | One page at a time, full size; the rail picks it (Alt+1 to Alt+3, ↑/↓ or the wheel over the rail). The pages sit in a column in rail order and the column slides up or down to the one picked | Building one thing: wiring a flow, setting up the station, reading a run |
| **Commander** | Station and Flow side by side for editing; the Board is a drawer under them. Closed, the drawer is just its head, with the latest of up to three number readouts; click the head (or its chevron key) to open or close it, drag the head to any height. Run opens it; it stays as left (kept in the browser). Each head has **open**, which shows that page alone in Navigator | Editing, then running and watching |
| **Lookout** | For whoever runs and watches a rig (IoT, monitoring): the Board fills the window, with Station and Flow as drawers at its left and right. Closed, a drawer is a standing tab (its icon and name); click it to open, click the head or its chevron to close, drag the head or the inner edge to any width. The run dock sits over the Board. Drawer states are kept in the browser | Running and watching a finished rig |

Switch with the view switch on the bar, or Alt+M (it cycles the three); the rail steps aside
into Commander or Lookout and back. Every page stays mounted in every mode (the pages not in view in Navigator are inert),
so switching never loses a canvas, a scene or a run; a canvas fits itself again when its
size changes. Page names are words, not numbered: Station, Flow, **Board** (the run page;
`dashboard` in code and the protocol).

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ⛵ paperboat  File  Edit  View  Run            [ navigator ]  commander  ● engine │
├─────────┬────────────────────────────────────────────────────────────────┤
│   ⬡     │ Flow                                                 6 blocks  │
│ station │                                                                │
│ ┌  ⌥  ┐ │        ┌ PID ───── fn ┐     ┌ Sum ───── fn ┐                   │
│ │ flow│ │        │ kp      40   │ ─── │ a            │                   │
│ └     ┘ │                                                                │
│   ▦     │      ┌ ▶ Run  ■ Cancel  for ░10░ s ░1×░  running 3.2 / 10 s ┐    │
│  board  │                                                                │
└─────────┴────────────────────────────────────────────────────────────────┘
```

Below 860px the rail moves to the bottom (a row of icon and name, the brackets travel
sideways), the bar wraps, and Commander stacks its pages in one scrolling column. Below
1560px the view switch drops its small label.

## Menus, command search and the run dock (#78)

The bar holds only the wordmark, the **menus**, the project's name (centred on the bar like a window title, in muted monospace so it isn't taken for a key; after the menus below 1100px; a signal-coloured dot
while there are changes not saved), the view switch, an error count when the Console has
errors, and a connection chip (which opens File > Backend…). Everything else
you can do is a **command** in one registry (`src/commands.ts`); the menu bar, the command
search and the registry's own shortcuts all read it, so a label, a shortcut and whether
something is available can't drift between them.

- **Menus** (`Menus.tsx`): File, Edit, View, Run, and only items for things the app does.
  An ARIA menubar: Tab enters it once, ←/→ move between menus, ↓ or Enter opens one, Esc
  closes. The open title is boxed in ink; a chosen item
  (page, mode, theme, a toggle) sits in brackets, never filled. A command that can't run
  now stays and says why in its tooltip ("Stop the run to edit the station").
- **Command search** (Ctrl+K, ⌘K on a Mac): a field floating at the top centre, over
  everything. It is only reached by its shortcut and View > Command search…, so the bar
  has no search well. Type to narrow, ↑/↓ and Enter to run, Esc to close; focus returns to
  where it was. The key is `SEARCH_CHORD` in `commands.ts`. `/` stays quick add on the
  canvas.
- **Run dock** (`Dock.tsx`): Run, Cancel, the next run's length and speed, and where the
  run is, floating at the bottom of the pages. It is glass, because it floats over live
  content (the one place glass is for). In Lookout it sits over the Board.
- **Projects** (#82, `project.ts`, `ProjectUI.tsx`): File > Open (Ctrl+O), Save (Ctrl+S), Save as,
  Restore last session and Recent projects; a file dropped on the window opens too. Anything
  that needs a word first (another backend, unsaved changes, a file that can't be read, the
  offer to restore) is a **notice** floating in the middle, with its safe choice last (Esc);
  a successful open leaves a **toast** at the bottom left with one **Undo**. See
  `docs/api/project.rst` for the file.
- **Keys:** a menu, the search and the dock's buttons take their keys (arrows, Enter) for
  themselves: elements marked `data-own-keys` are not forwarded to the backend. A
  command's `chord` is handled by the registry (Alt+1–3, Alt+M, Alt+V, Ctrl+K); undo and
  copy/cut/paste stay with the canvas, which the Edit menu reaches through `FlowControls`.

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

**Component**: `--bar-height`, `--rail-width` (fits "station" with room for the brackets),
`--panel-head-height`, `--gutter`, `--panel-pad`, `--select-outline`, `--select-offset`,
`--slide` / `--slide-time` (a page sliding in, the rail stepping aside, the Board drawer).

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
| **Bar** | The wordmark (the logo mark and the name), the backend (a well with the address and engine name; Connect), the run (Run, length, speed, Cancel, status in words), view and theme, divided by a hairline |
| **Rail** | The pages share the rail's full height on sunken grey, each an icon over its name in monospace (no numbers). The current one sits in 2px square ink brackets, like a chosen switch option; it is ink, the others grey, its icon lifted a little. On a change the pair glides to the new key as one, on the page column's curve and time (`--slide`), so rail and page move together; the icons and names ease in step. Placed from the key's face plus 8px, so they never touch the name |
| **Panel** | Grey, 1px edge, 6px radius; a head with its number, name and a short count on the right |
| **Block** | Raised grey, 1px outline; head with the name and a word tag (deleted from the selection dialog or the Delete key). Step blocks are outlined in ink; a linked block carries its object's 3px ink bar on top, the Station glyph and its id in its head and the tag `linked`; missing ones dashed red (dashed means missing, nothing else); failed ones outlined red. Params are underlined values |
| **Station object** (#53) | On the Station graph: a square-cornered plate (no radius) with a 3px ink bar on top, a joint's bar doubled; head with the name, its id (after the Station glyph, the rail's icon, when it has a linked block on the Flow, matching that block) and the tag `object` or `joint`. Its link slots are ports on the left; the Station glyph and `block` mark the output of one with a linked block. Reads in monochrome: shape, not colour |
| **Compact block** | A simple block (`view.compact`, from the catalog) as a 64×56 plate: raised grey, 1px outline, 5px radius, with a 30px **glyph** in ink (a transfer curve for gain, sum, threshold and saturation, a few mono characters for logic) and bare 8px port dots; no name rows. Its name and main value sit in a caption under the plate (12px sans, the value in mono with a hairline under it). A port's name shows on hover. State is said in words: the caption's name becomes `running` (signal colour, plate outlined in it, progress as a 2px line along the bottom) or `failed` (red). Selected: the 2px outline set 4px off the plate. Click opens its params in a popover (the glass, wells for values; Escape or a click outside closes it); double-click unfolds the plate into the full card in place, with a **fold** key in its head to put it back. A block with no `view`, a missing block, and a step block are always cards |
| **Port** | Its name, and its unit after it in small grey monospace (`force N`, `m x`). A wire only snaps to a port it fits: the same type (`any` takes anything but a step) and, when both have one, the same unit. Inputs sit left, outputs right, sharing rows (the first input beside the first output); drag a port's name (grab cursor) to the side nearest the pointer or along its side, the card reflowing live as it goes; double-click puts it back. Top and bottom ports sit centred in a strip above the head or below the params, hairline-divided, their dots on the card's edge |
| **Wire** | Ink, 1.2px; step wires 2px; image wires dashed; selected wires 3px |
| **Subsystem / story / frame** (#68) | A subsystem or story is a block card with a second card edge offset 4px behind it (grey; ink for a story): there is more inside. Head: its name, the tag `subsystem` or `story` (or `running` / `failed`: its busiest block's state), an **open** key and ⊟ (ungroup). Its ports are the wires crossing its edge, named `node.port`, movable like a block's; a story also has its step ports `in` and `next`, and its `seconds` and `fresh` as params. Double-click (or open) shows its level: a breadcrumb floats top-left (glass: `Flow › Name`, earlier levels as underlined links, this one's name editable, its kind after), Alt+↑ or Escape (with nothing selected) goes up. Inside, a wire from outside comes in through an **In** pill at the left and leaves through an **Out** pill at the right (sunken grey, 1px outline, the tag in sans, the outside port in mono). A frame stays a dashed box round its blocks; ▾ collapses it to one block |
| **Instrument** | A display with its name (and latest value) underneath; readouts show a 30px monospace number. Four sizes on a grid of equal cells (116px tall, 12px apart), picked S, M, L, XL: **compact** 1×1 (a readout), **normal** 2×1 (a plot to read at a glance), **large** 2×2 (a plot or picture to study), **xlarge** 3×3 (the one to fill the page with); a kind's default is display compact, graph normal, image large. Before a run sends anything it is a placeholder: same frame, greyed, dashed edge, "–" |
| **Arrange** | The Board head's key. While on: a dashed outline round each instrument, faint dashed cells under the grid, a size picker (C N L in brackets, full names on hover) top right of each, a solid outline where a drag would land. Drag to a cell, or focus and use the arrows. Reset layout returns to the backend's default. The bar also adds decorations (Section, Label, Console, Separator — and |) and a tab; a picked decoration gets the solid selection outline, a remove key, a corner grip to resize (a separator's end to lengthen) and, for a label, a bracketed `heading` / `note` switch. Double-click text to edit it where it stands; while editing, a row of value chips (one per monitor) puts `{block}` in at the caret on click, or by drag |
| **Decoration** | On the Board grid, from the layout (#65, #76). **Section**: a titled frame, 1px line, the title sits on the edge on the panel colour; takes no cells. **Separator**: a 1px line in the gap, horizontal or vertical. **Label**: text on the grid, `heading` (600 weight) or `note` (muted, small); takes cells. **Console**: the Console's lines in a screen-coloured frame; takes cells. Text is a formatted string: `{block}` shows the monitor's latest value in monospace tabular figures ("–" and muted before a run) |
| **Tabs** | The Board's pages as a bracketed choice switch above the grid, shown from two tabs; double-click (or F2 on a focused tab) renames it in place, in or out of Arrange: Enter or clicking away keeps it, Esc drops it, a name another tab has is outlined red and not taken; **+ tab** opens the new tab already renaming; × removes an empty one, dropping an instrument on a tab name moves it there |
| **Console** | A floating window, centred, opened by the Console key beside Variables (its count shows errors; Esc closes); a display; numbered lines; warnings in the signal colour, errors red; a block's id is a key that shows it on the Flow page; hovering a line shows a **copy** key (plain text: `[level] block: message`, then `copied` for a moment), and the head has **Copy all** |
| **Card** | A block or station object in the browser: name, kind in words, one-line description, ports on a well (in → out; step ports square), params with defaults, how many are already used, and Add. Draggable |
| **Collection window** | The browser, floating over the page from its Add key: search, a kind choice in brackets, packages on the left, cards in sections. Moved by its head and remembered; Escape closes. While a card is dragged it fades and lets the pointer through |
| **Quick add** | A search floating at the cursor (`/` over the canvas, or a double-click on empty canvas): the best matches, arrows, Enter adds at that spot |
| **Selection dialog** | Floats above what's selected (below it when that is at the top of the canvas) (one or more blocks or groups, with any wires), like quick add: raised grey, ink outline. Says what's picked in words ("3 blocks · 1 wire"), then Delete, Group (a subsystem), Story, Frame, Ungroup, each greyed when it doesn't apply; linked blocks stay and it says so. Hides while dragging; Escape clears the selection |
| **Canvas help** | No permanent help under the canvas. An empty canvas says "/ or double-click to add a block" in its middle, in grey. A round `?` key at the canvas's bottom left opens the gestures as a two-column list (what · how) on glass; Esc or a click elsewhere closes it |
| **Drop hint** | While a card is dragged over the Flow: a dashed outline where it lands, a thickened wire it would be inserted into, or a dashed line to the port it would connect to; with a label in words ("Insert Lag: pid.u → u, y → sum.a") |
| **Port (while a wire is dragged)** | A 24px hit area round the 8px dot. A drop within 40px of a port that accepts the wire joins it (a wire's end is grabbed within 20px; both in `SNAP`, `wireSnap.tsx`). The port it would join is enlarged, ringed, in the signal colour, with its name bold; ports that can't take the wire (wrong type, same block) dim to 30%. The nearest port that accepts wins, not the nearest port |
| **Variables sheet** (#36) | A drawer at the right edge of the pages in every view: 360px in Navigator, 330px in Commander and Lookout, a bottom sheet below 860px. Closed, it is a standing tab like Lookout's side drawers (chevron, `=`, "Variables", the count); click it, Alt+V or Edit > Variables opens it, sliding out on `--slide` with its content uncovered at full width, never squeezed; the head's chevron, Alt+V or Esc closes it. Rows by group: name in monospace (drag it onto a field to bind, or onto a Board label or section title to show its value there: `{name}` goes at the end of the text, which is outlined dashed while the variable is over it, in or out of Arrange), type in words, value in a well, unit, "2 uses" or "unused". A number's well is one field like a spreadsheet cell: a number or a formula over other variables (`kp_theta / 5`); it shows the result, and the formula while editing (and on a line below the row); a number applies as you type, a formula on Enter. The open row has an ink outline: name, type as bracketed choices, value, unit, range, note, group, where it's used (Show), Reset to default or Delete. Errors in words, red. Copy as TOML in the foot |
| **Bound field** | A param or property bound to a variable: the variable's name in an ink outline (a formula marked ƒ), what it comes to after it in grey. Click: edit the variable in place, Show in Variables, Unbind (keeps the value). Typing **=** in a number field lists the variables that fit, takes a formula, or makes a variable of the value. A variable dragged over a field it fits gets a dashed outline |
| **Floating panel** | The only glass: what floats over live content (the station's properties). One token (`--float-blur: none`) retires it |
| **View cube** | A 96px cube in the 3D Station's top-right corner (hidden in 2D views): a mono label on each face, ink edges, turning with the camera. Hover marks the third of the cube a click would pick (face, edge or corner) in the signal colour; click eases the camera there (260ms, none with reduced motion); drag orbits; a round Home key (a house) centred under it eases back to the opening view: grey while the view is already there, ink once it has moved. The opening view is the backend's `view.camera` (eye, target, fov), else eye [2, 4, 8] at the origin |

The Variables are shared by the Station and the Flow, so they belong to neither page: a sheet beside the page you're working on, never a page of their own (#36 weighs the options).

On a canvas a drag pans and the wheel zooms; Ctrl-drag draws a dashed selection box (touching a block picks it), Ctrl-click adds one. The selection's actions are in its dialog, not in a toolbar. Space is never a canvas key: it belongs to the backend.

Focus is a 2px ink ring. Disabled keys are grey and faded.

## Logo

A two-tone paper boat in pixels over a row of larger water pixels. The boat is 31 columns
of 2-unit pixels: a sail rising to a peak, light on the left half and shaded on the right,
a gap, then a wide hull whose ends taper to points. The water is five 6-unit pixels, so
the whole mark is 62 × 42 units. The pixels are hard-edged (`shape-rendering:
crispEdges`); never smooth, fill with gradients or add shadow.

| Where | Colours |
| --- | --- |
| **Bar and in the app** ([`Logo.tsx`](src/Logo.tsx)) | From the theme: sail light and hull in `--text`, sail shade in `--text-muted`, water in `--signal`. Mono, with the one colour as the water |
| **Favicon and app icon** ([`public/favicon.svg`](public/favicon.svg)) | The brand palette, `--brand-*` in `tokens.css`, on a rounded tile: `#241f31` ground, `#e4e0ea` sail light and hull, `#8d849e` sail shade, `#c6738f` water. A muted violet |

The name is always lowercase, Geist Sans 600, tracking −0.01em, after the mark. Below 24px
tall the mark is still readable; below 16px use the favicon. Keep the favicon and
`Logo.tsx` on the same geometry: change both together.

## Motion

Only in answer to an action, and all on one ease (`--ease`, an even in-and-out), so the app
moves as one thing; only the time differs with the distance:

| Token | Time | For |
| --- | --- | --- |
| `--motion` | 150ms | Hover, colour |
| `--glide` | 300ms | A choice moving: the brackets of every bracketed choice (view switch, Kind in the block browser, Board tabs, an instrument's size, a label's style) glide to the new option instead of blinking there (`GlideBrackets` in `Shell.tsx`); a selection settles in (a block's outline closes in from 8px further out; a wire thickens); a popover rises in |
| `--slide` | 560ms | A surface moving: a page sliding into view, the rail's brackets (in step with the page), the rail stepping aside, a drawer |

Nothing moves on its own; with reduced motion nothing animates (the tokens drop to 0).

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

## Docs

The docs follow the same tokens as the app, from one source (`apps/web/src/tokens.css`,
synced into `docs/api/_static/tokens.css` by `scripts/sync-doc-assets.sh`). A docs-only
value is not allowed: it goes in as a token first. The API reference (`docs/api`) and the
plan page (`docs/plan.html`) use Geist and Geist Mono self-hosted from
`docs/api/_static/fonts/` (no CDN), light and dark follow the system and can be switched
with the bracketed `[ auto ] light dark` control remembered in the browser. Code samples
stay monochrome (`highlight_language = "none"`). `sphinx-build -W` must stay clean.
