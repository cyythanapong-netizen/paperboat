# Paperboat design system

How the web app looks and why. The look is **glass over a Bauhaus composition**: frosted
panels float over a still ground of a square, a triangle, a circle and a bar in the
Bauhaus primaries. The glass gives depth and keeps the work in front; the Bauhaus gives
the order: geometric shapes, primary colours that mean something, black rules, and one
geometric typeface.

Tokens live in [`src/tokens.css`](src/tokens.css), the rules in [`src/styles.css`](src/styles.css),
and the frame (view modes, rail, panels, glyphs) in [`src/Shell.tsx`](src/Shell.tsx).

## Principles

1. **Colour is information.** Every colour stands for a page, and appears only where
   something belongs to that page. Nothing is coloured for decoration.
2. **Glass holds the work; content sits on solid ground.** Panels, the bar and the rail
   are frosted glass. What the user reads or edits on them (blocks, instruments, fields)
   is near-opaque, so wires, plots and numbers never compete with the background.
3. **One bold thing.** The composition behind the glass is the identity. Everything in
   front of it is quiet: black outlines, white surfaces and one typeface.
4. **Tactile commands, quiet controls.** The few commands on the bar (Run, Cancel,
   Connect, the view mode) have a hard offset shadow and press in. Every other control is
   a plain black outline.
5. **Simple to enter.** A first-time user sees one page (Navigator), a Run button and the
   flow the backend opened with. Nothing needs setting up before the first run.

## Pages: a colour and a shape each

Kandinsky's Bauhaus correspondence of colour and form, one per page. The glyph shows on
the rail, on the panel head, and in the composition.

| Page | Colour | Shape | Also marks |
| --- | --- | --- | --- |
| **Station** | red `--page-station` | square | blocks that act on the station (source, probe, linked blocks) |
| **Flow** | yellow `--page-flow` | triangle | what is running now (a path's block, a selected wire), Run's shadow |
| **Dashboard** | blue `--page-dashboard` | circle | monitor blocks, plot traces, readout edges |
| **Console** | black `--page-console` | bar | step blocks and step wires (control), the log |

Yellow is never text and never a fill behind small text: it is for shapes, edges and
states only, with black on or around it.

## View modes

| Mode | What it shows | When |
| --- | --- | --- |
| **Navigator** (default) | One page at a time, full size; a rail on the left picks it (Alt+1 to Alt+4). The page's shape comes forward in the composition. The Console's rail item shows a red count of errors | Building one thing: wiring a flow, setting up the station, reading a run |
| **Commander** | Every page at once, as glass tiles: Station and Flow on top, the Dashboard and Console below. Each head has **Open**, which shows that page alone in Navigator | Running and watching: change a param, see the station and the plots react |
| **Monitor** (planned) | A finished program's Dashboard, Station and Console collapsible | Using a rig someone built |

Switch with the control at the right of the bar, or Alt+M. The mode and the page are
remembered in this browser. Every page stays mounted in both modes, so switching never
loses a canvas, a scene or a run; the Flow canvas fits itself again when its size changes.

```
Navigator                                    Commander
┌──────────────────────────────────────┐     ┌──────────────────────────────────────┐
│ ⛵ Paperboat ● backend  (Run) … [N|C] │     │ ⛵ Paperboat ● backend  (Run) … [N|C] │
├────┬─────────────────────────────────┤     ├──────────────────┬───────────────────┤
│ ■  │ ▲ Flow                          │     │ ■ Station   Open │ ▲ Flow       Open │
│ ▲  │                                 │     │                  │                   │
│ ●  │      the page, full size        │     ├──────────────────┴─────┬─────────────┤
│ ▬  │                                 │     │ ● Dashboard       Open │ ▬ Console   │
└────┴─────────────────────────────────┘     └────────────────────────┴─────────────┘
```

Below 760px wide the rail moves to the bottom, the bar wraps, and Commander stacks its
pages in one scrolling column.

## Tokens

Three layers. Components use semantic and component tokens only: never a primitive,
never a raw hex.

**Primitives** (raw values)

| Token | Value | Use |
| --- | --- | --- |
| `--paper` | `#edeff2` | the ground: cool white, not cream |
| `--ink` | `#000000` | rules, outlines, glyphs: true black |
| `--graphite` / `--slate` | `#23252b` / `#5d626d` | text, secondary text |
| `--mist` | `#c9cdd4` | hairlines on glass |
| `--red` `--yellow` `--blue` | `#d02020` `#f0c020` `#1040c0` | the Bauhaus primaries |
| `--glass-55` / `-75` / `-92` | white at 55 / 75 / 92 % | panels / overlays / content |
| `--font-sans` | Jost (self-hosted) | everything: a Futura-lineage geometric sans |
| `--font-mono` | JetBrains Mono | the Console only: its lines are logs and code |
| `--size-xs…xl` | 11 13 16 21 28 px | a perfect-fourth scale from 13 px |
| `--space-1…6` | 4 8 12 16 24 px | a 4 px grid |

**Semantic** (roles): `--page-*`, `--surface-ground | glass | glass-strong | content | edge`,
`--glass-blur` (18 px, saturate 140 %), `--text`, `--text-muted`, `--text-on-ink`,
`--rule` (2 px black), `--hairline`, `--state-running` (yellow), `--state-failed` (red),
`--state-ok` (green, deliberately not a page colour), `--focus` (blue), `--shadow-hard`
(3 px offset black, controls only), `--radius-square` (0), `--radius-round` (pill and
circle), `--motion`.

**Component**: `--panel-*`, `--rail-width`, `--bar-height`, `--gutter`, `--button-*`,
`--block-*` (the head colour per kind), `--wire*`, `--trace`.

## Components

| Component | Rule |
| --- | --- |
| **Bar** | Glass, a 2 px black rule below. Left to right: wordmark, backend (status dot, address, Connect, engine name), run controls (Run, length and speed, Cancel, progress), view mode |
| **Run** | The only filled button: a black pill with a yellow hard shadow. Disabled while a run is going |
| **Rail** | Glass. Each item is the page's glyph over its name; the current one is white with a 4 px black bar on its inner edge |
| **Panel** | Glass, square corners, a 4 px top edge in its page's colour, a head with glyph and name (16 px, 600). No drop shadows |
| **Block** | Near-solid white, 1.5 px black outline, square. The head says where it acts: white (function), red (source, probe), blue (monitor), black (step). Linked blocks have a dashed outline; missing ones are red and dashed; a running one has a yellow outline, a failed one a red outline |
| **Port handle** | Signal: a small round dot. Step: a black square. Image: violet |
| **Wire** | Graphite, 1.5 px; step wires black, 2 px; selected wires yellow, 3 px |
| **Group / story** | A visual group is a dashed frame; a story (a step) a solid black frame with a black head |
| **Instruments** | A readout is a white card with a blue edge and a 28 px tabular number; a plot is a white field with a blue trace over a black baseline |
| **Console** | Monospace, numbered. Warnings sit on a yellow wash, errors are red; a block's id is a button that shows it on the Flow page |

States everywhere: hover lightens the glass (or fills yellow in the palette), focus is a
2 px blue ring, disabled is 40 % opacity without a shadow, pressed moves a tactile button
into its shadow.

## Motion

One orchestrated moment: in Navigator, the composition brings the page's shape forward
(700 ms) when the page changes. Responses to actions are fast (60 to 200 ms). Nothing
moves on its own, and with reduced motion nothing animates at all.

## Accessibility

Text is at least 4.5:1 on its surface (content surfaces are 92 % white). Every control
is reachable by keyboard with a visible focus ring; the rail is a `nav`, the view mode a
radio group, each page a labelled `section`. Colour is never the only signal: pages also
have shapes and names, states also have outlines and words.

## Doing more

- A new page gets a colour and a shape from the same vocabulary, a glyph in `Shell.tsx`,
  a place in both modes, and a row in the table above.
- A new component uses existing tokens; if it needs a new value, add a primitive, then a
  semantic role, then use the role.
- A backend's own look belongs in a web station (`[station] view = "web"`), not in the
  app's styles.
- Not yet: a dark theme (the semantic layer is where it will switch), the Monitor mode,
  the 2D station's flat look.
