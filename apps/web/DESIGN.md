# Paperboat design system

How the web app looks and why. The look is **flat mono, like a well-made instrument**:
grey panels with hairlines, clear sentence-case labels, recessed grey fields for what you
type, black displays for output, and one colour that means "live". It takes its spirit from hardware such as Teenage
Engineering's: honest, precise and quiet, with a little character in the type and the
displays. Flat: no bevels, no indicator lights; shadows only on what floats.

Tokens live in [`src/tokens.css`](src/tokens.css), the rules in [`src/styles.css`](src/styles.css),
and the frame (view modes, rail, panels, switches) in [`src/Shell.tsx`](src/Shell.tsx).
The direction and its history are in issue #48.

## Principles

1. **Mono first.** Ink on grey. Hierarchy comes from weight, size, spacing and lines,
   not colour.
2. **One colour, one meaning.** The signal colour (orange for now, a placeholder until
   #48 settles it) means live or look here: a run in progress, a block that is running,
   a trace, a warning. Red means failed. Nothing else is coloured, with one exception: a
   **Scope**'s channels (#37) take `--trace-1` … `--trace-8` (signal, cyan, indigo, magenta,
   gold, violet, green, pink), made for the black screen and checked for colour-blind
   separation. Slot 1 is the signal colour, so one channel matches the Graph; there is no
   red; channels 5–8 are also dashed and 9–16 dotted, so crossing traces never rely on
   colour alone; a channel keeps its slot when others hide. Each also has its legend label.
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
dark) or with the theme key at the bar's right end (one click cycles them); the choice is kept in this browser. The 3D station reads its background and grid
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
errors, a connection status pill (which opens File > Backend…) and the theme key. The pill
(`StatusPill`, shared with Squero Studio) keeps the room of its widest label, so `connecting` →
engine name never shifts the keys beside it. Everything else
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
- **Arrange Blocks** (#103): View > Arrange Blocks lays out the visible node canvases left to
  right, following their wires; the Flow and Station-graph heads have an **Arrange** key for
  their own canvas. The deterministic layout (`graphLayout.ts`) ignores back-edges in loops,
  orders each layer by its neighbours, and uses measured block sizes. Flow arrangement works
  inside the open level and every group below it, refits visual frames, and is one undo step;
  Station arrangement is one Station edit. Both are disabled during a run.

## Tokens

Three layers. Components use semantic and component tokens only: never a primitive,
never a raw hex.

**Primitives**: greys (`--grey-50` to `--grey-900`) for light, `--night-*` for dark,
`--black`, `--white` and `--screen-*` for displays, `--orange` (placeholder) and `--red`; fonts
`--font-sans` (Geist) and `--font-mono` (Geist Mono), both self-hosted; sizes
12 / 12.5 / 13 / 15 / 30 px; a 4px spacing grid.

**Semantic**: `--surface-ground | panel | raised | sunken | well | screen | float`, `--well-shadow`, `--float-blur`, `--text`,
`--text-muted`, `--text-on-screen(-muted)`, `--line` (dividers), `--line-strong` (keys,
blocks, fields), `--line-ink` (wires, switches, selection), `--dots`, `--signal`,
`--danger`, `--focus`, `--text-on-signal` (text on a signal fill), `--shadow-0 | 1 | 2 | up`
(elevation: knob, popover, dialog, rising sheet; stronger in dark), `--grid-line` and `--screen-line`
(plot grid, and hairlines and selections on the black screen), `--scene-bg`, `--scene-grid`, radii, `--motion`. Dark redefines these
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
| **Custom block look** (#160) | A block with `view.icon` draws its own picture on the compact plate instead of a glyph: 64×56 (1×1), 128×56 (2×1) or 64×112 (1×2), the 0..100 box inset 8px. Strokes are the glyph's: ink `--text`, muted `--line-strong`, signal `--signal` (live state only, never decoration); weights 1, 1.5 and 2px, round caps and joins; the `axis` dash is `2 2` and is never red (dashed red means missing, nothing else); fills are none, `--text`, `--surface-raised` or `--surface-sunken`; text is 12 / 12.5 / 13 / 15 / 30px in sans or mono. Only tokens reach the SVG. A picture that can't be drawn whole falls back to the plain plate. State (selected, running, failed) is the plate's, not the picture's. **Connector ports** sit on the picture's edge at the descriptor's `side` and `at`: square, diamond, double ring, triangle or circle at 8, 10 or 12px, filled or hollow by the a/b or p/n side; the hit area stays 40px. Their wires are 1.2px (Pin, signals), 1.8px (flanges) or 2px (fluid); thermal wires are dashed grey. Three or more wires at a port draw a 4px filled junction dot; an `array` port shows its count; a port hidden by its `if` leaves its wires dashed red. A port that can't take the wire dims, and its hover says why in words |
| **Port** | Its name, and its unit after it in small grey monospace (`force N`, `m x`). A wire only snaps to a port it fits: the same type (`any` takes anything but a step) and, when both have one, the same unit. Inputs sit left, outputs right, sharing rows (the first input beside the first output); drag a port's name (grab cursor) to the side nearest the pointer or along its side, the card reflowing live as it goes; double-click puts it back. Top and bottom ports sit centred in a strip above the head or below the params, hairline-divided, their dots on the card's edge |
| **Wire** | Ink, 1.2px; step wires 2px; image wires dashed; selected wires 3px |
| **Subsystem / story / frame** (#68) | A subsystem or story is a block card with a second card edge offset 4px behind it (grey; ink for a story): there is more inside. Head: its name, the tag `subsystem` or `story` (or `running` / `failed`: its busiest block's state), an **open** key and ⊟ (ungroup). Its ports are the wires crossing its edge, named `node.port`, movable like a block's; a story also has its step ports `in` and `next`, and its `seconds` and `fresh` as params. Double-click (or open) shows its level: a breadcrumb floats top-left (glass: `Flow › Name`, earlier levels as underlined links, this one's name editable, its kind after), Alt+↑ or Escape (with nothing selected) goes up. Inside, a wire from outside comes in through an **In** pill at the left and leaves through an **Out** pill at the right (sunken grey, 1px outline, the tag in sans, the outside port in mono). A frame stays a dashed box round its blocks; ▾ collapses it to one block |
| **Instrument** | A display with its name (and latest value) underneath; readouts show a 30px monospace number. Four sizes on a grid of equal cells (116px tall, 12px apart), picked S, M, L, XL: **compact** 1×1 (a readout), **normal** 2×1 (a plot to read at a glance), **large** 2×2 (a plot or picture to study), **xlarge** 3×3 (the one to fill the page with); a kind's default is display compact, graph normal, image and scope large. Before a run sends anything it is a placeholder: same frame, greyed, dashed edge, "–" |
| **Graph and Scope** | **Graph** is the glance: the shape of one signal (a vector: one dashed variant per component) and its latest number; no axes, no controls. **Scope** (#37) is the study: canvas (uPlot), one y axis per unit (first left, second right, the rest share the left), units in the screen's corners, a legend of line sample + source port + value + unit (click to hide). The screen is output only; the caption holds the name, the state in words (`live`, `frozen`, `armed`, `triggered`; signal colour when live), the mode choice `[ time ]  xy  fft`, then `+ tap  freeze  cursors  trigger  ⋯` (a toggle that is on in brackets). At normal size only ⋯, which then holds everything. Gestures: drag pans (and freezes), Ctrl+wheel zooms time, Shift+Ctrl+wheel zooms y, Ctrl-drag zooms to a box, double-click autoscales and follows; F, C, T, A while focused. Cursors are dashed ink lines with tabs 1 and 2 and a readout strip; the trigger is an orange T on the right edge (drag it) and ▼ at the trigger point. Draw styles `line`, `line+points`, `points`, `smooth` (monotone, never overshoots) |
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

A two-tone paper boat over a row of larger water pixels. The boat is 31 columns of
2-unit pixels: a sail rising to a peak, light on the left half and shaded on the right,
a gap, then a wide hull whose ends taper to points. The water is five 6-unit pixels, so
the whole mark is 62 × 42 units. The pixels are hard-edged (`shape-rendering:
crispEdges`); never smooth, fill with gradients or add shadow. The water is the signal
colour and means "live".

**Small-size variant (S2).** Below ~32px the 2-unit pixels stop reading, so the mark
switches to a smooth geometric form: a two-tone triangle sail (light left, shade right)
and a trapezoid hull, keeping the original five pixel water dots. The 62 × 42 viewBox is
unchanged, so the mark's footprint and spacing never change.

- `height >= 32`: pixel mark (the brand).
- `20 <= height < 32`: S2 with a 2-unit gap between the sail's base and the hull's top.
- `height < 20`: S2 with the sail and hull merged (no gap) for legibility.

The bar logo (24px) is S2. The favicon (64px, brand palette) stays pixel art.

**Water ripple (W2).** The water means "live". A ripple passes through it now and then:
the five dots lift in sequence (a crest travelling left → right, 3.75px of travel at the
32px+ size) and the whole mark bobs ~1.5px as the crest passes under it, on the existing
`--ease`, 1.4s, transform only, no layout shift.

- Idle (no run): a ripple every ~6s, ripples for ~7% of the cycle, then rests.
- Run live: a faster continuous ripple (1.4s) while a run is in progress, plus a single
  bob when the run starts. `running` is threaded from `App.tsx` through `Wordmark` into
  `Logo`.
- Reduced motion: `@media (prefers-reduced-motion: reduce)` sets the logo animations to
  `none`.

Ripple amplitudes scale with the mark (`--lr`, `--lb` set on the svg by `Logo.tsx`).

| Where | Colours |
| --- | --- |
| **Bar and in the app** ([`Logo.tsx`](src/Logo.tsx)) | From the theme: sail light and hull in `--text`, sail shade in `--text-muted`, water in `--signal`. Mono, with the one colour as the water |
| **Favicon and app icon** ([`public/favicon.svg`](public/favicon.svg)) | The brand palette, `--brand-*` in `tokens.css`, on a rounded tile: `#241f31` ground, `#e4e0ea` sail light and hull, `#8d849e` sail shade, `#c6738f` water. A muted violet |

The name is always lowercase, Geist Sans 600, tracking −0.01em, after the mark. Keep the
favicon and `Logo.tsx` on the same geometry: change both together.

### Interactive Logo Guides
- **Final specification & animation guide**: [`docs/design/logo-final.html`](../../docs/design/logo-final.html) (interactive scale ladder from 12px to 64px, ripple motion, S2 geometric variant, and color tokens).
- **Variant exploration**: [`docs/design/logo-variants.html`](../../docs/design/logo-variants.html) (comparison of all geometric, pixel, and ripple explorations).

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

## Squero Studio OM

Squero Studio: OpenModelica (everyday **Squero Studio OM**, launched by `squero-om` and
`paperboat squero`) is a separate app for building the equipment models that Paperboat runs.
Following the ISA-88 boundary (GUIDELINES §4): Studio builds equipment and exports `<model>-cpp/`
backends; Paperboat runs procedures (PID, step stories) and monitors them on the Board. Studio's
Simulate is a local check, never a second Flow page.

The top bar shows the Squero Studio lockup (W3 Water wordmark: "squero" in bold ink over five signal dots that ripple every 5 s when idle and continuously while Simulate runs, light "studio", and the square B3 Mono "OM" product badge)
along with the project name and an `omc <ver> · ready | checking | failed` status pill (the same `StatusPill`) and the theme key on the right.
Studio has no modal dialogs; refusals and notices appear in plain words.

Surfaces:
- **Library** (left): lazily loads Modelica Standard Library packages and project classes; each leaf
  displays its imported `view.icon`. Filter input searches by name then qualified path.
- **Acausal canvas**: diagram authoring on `@xyflow/react` with shared `Compact`, `Icon`, and ports.
  Undirected acausal wires, directional causal arrows, junction dots on 3+ wires, and clear word errors
  on refused connections (`an output joins only an input`). Quick add (`/`) adds parts at the cursor or
  splices directly into a selected wire (`Insert R2: R1.n -> p, n -> L1.p`) as a single undo step.
  Every edit calls `omc` directly (`addComponent`, `addConnection`, `setComponentModifierValue`),
  keeping `.mo` files OMEdit-compatible.
- **Split view** (right): inspects `[ .mo ]` code alongside generated `c++` (`ode` or `fmu`) from a dry run.
  Simulate runs a local omc simulation (uPlot check plot of chosen variables); variable chips toggle traces.
- **Component popover**: opens on clicking a canvas part. Shows parameters as `NumberField` rows; `=` binds
  a parameter to a top-level parameter (creating it if absent); ports provide `+ Probe` and `+ Input` actions.
- **Interface sheet** (`Alt+V`): edits Station properties (top-level parameters and default values),
  configures entry pills (inputs on the left, outputs on the right), provides a live linked-block preview
  card, and includes the **Look tab** for editing custom block icons (§3 primitives on a 5-unit grid,
  with `Choice` pickers for stroke, weight, fill, and dash, plus light and dark preview tiles).
- **Dock**: live equation/unknown balance from `checkModel` (e.g. `balanced · 76 / 76`), dangling flow
  connectors with domain fixes (`L1.n unconnected [ Fix: Ground ]`), and `Export backend` with a 2px signal
  progress bar.

## Docs

The docs follow the same design system (#72): the API reference (`docs/api`, Sphinx) and the
plan page (`docs/plan.html`) use the app's tokens and fonts, so a change to a token changes
all three. `docs/gen_tokens.py` writes `docs/_static/tokens.css` from `src/tokens.css` (its
font imports become `@font-face` rules for the Geist files in `docs/_static/fonts`, so the
docs make no network requests); CI fails if it is out of date. Run it after changing a token.

- **API reference:** a theme on Sphinx's `basic` (`docs/api/_themes/paperboat`), rules in
  `docs/_static/docs.css`. The rail on the left holds the contents (the current page boxed in
  ink, the rest grey), search (a well and a key) and the theme choice; the page is a panel.
  Text is 15px sans (reading, not UI), code is mono in a recessed grey well, signatures are
  raised grey plates with the name in weight, tables have hairlines only. Links are ink with
  an underline, never blue; code has no colour beyond ink and grey. Notes are plates with a
  hairline: the signal colour for a warning, red for danger.
- **Themes:** light and dark follow the system; `[ auto ]  light  dark` in the rail picks one
  (kept in this browser, key `paperboat.docs.theme`, also read by the plan page).
- **Plan page:** its own names (`--bg`, `--accent`…) are aliases of the tokens, so it needs no
  palette of its own: ink and grey, with the signal colour for what is live.
- A docs-only value is not allowed: it goes in as a token first, like any other.
