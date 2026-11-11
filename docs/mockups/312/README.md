# pb#312 — Squero Studio Octave mockups

Six design studies for `squero-studio-octave`. They use the real tokens from
`@paperboat/shared/src/tokens.css` and the flat-mono shell of `squero-studio-om`, not a
MATLAB/Octave clone. Each screen is a self-contained HTML page with a canvas height field
(`chart.js`), rendered to JPEG with Playwright at 1600×1000, 1.5×.

## Chart studies

| File | Screen | Idea |
| --- | --- | --- |
| `a-bench.jpg` | **Bench** | Workspace-first: variables as live cards, tie a card to a chart axis, scratch line previews its answer, linked cursor slices. |
| `b-split.jpg` | **Split** | Script-first: inline answer gutter, a chart lasso writes a line back into the `.m`, pinned views above the chart. |
| `c-board.jpg` | **Board** | Run-first: 3D chart plus heat, scatter and line as Board instruments, a story ribbon, camera keys and ghost compare. |

## App surfaces

| File | Screen | Idea |
| --- | --- | --- |
| `d-start.jpg` | **Start** | The app itself: resume cards that restore a session, attach-to-backend panel, recent backends, and a large "pick up where you left off" preview. |
| `e-editor.jpg` | **Editor** | The script surface: per-line run ribbon, collapsible cell cards with costs, inline answer chips, hover peek, and a line-scoped workspace snapshot. |
| `f-variables.jpg` | **Variables** | The panel and lens: grouped list with live previews, a matrix lens (heatmap, profiles, histogram, stats, cell edit), and a tie board of variable → chart bindings. |

## The UX ideas

1. **Variable cards, not a table.** Each variable carries its shape, type and a live glyph.
2. **Tie, don't configure.** Drag a card's ring onto an axis or port; rebinding is a gesture.
3. **Scratch with a preview.** Evaluate while typing; `↵ show` pins, `⇥ send to chart` binds.
4. **Inline answer gutter.** Every assignment shows its shape and a micro-preview.
5. **Lasso → variable.** A selection on the chart proposes the `.m` line that produces it.
6. **Time curtain + linked slice.** A vertical plane at the run time cuts the surface; its
   profile mirrors in the inspector.
7. **Pinned views.** A pin stores camera, curtain and series, not a screenshot.
8. **Ghost compare.** The previous run is a translucent wireframe with a delta readout.
9. **Charts as Board instruments.** Drag, resize, tab and section like every other Paperboat
   instrument; no floating figure windows.
10. **Camera keys and stories.** Saved viewpoints and run markers in one scrubber.
11. **Resume cards.** The start screen restores files, workspace, views and time — a session,
    not a recent-files list.
12. **Run ribbon.** The editor gutter shows where the run went: bar length is duration, orange
    is the cursor, a dot marks changed lines.
13. **Peek any name.** Hovering an identifier shows shape, value and a micro-preview in place.
14. **Line-scoped snapshot.** The workspace as of the cursor line, with a diff against the
    previous run.
15. **Lens, not a spreadsheet.** A matrix gets a heatmap, row/column profiles, a histogram,
    stats and in-place cell editing.
16. **Tie board.** A patch bay of variable → chart bindings, solid for live and dashed for ghost.

## Regenerate

```sh
cd docs/mockups/312
python3 -m http.server 8801 --bind 127.0.0.1
BASE=http://127.0.0.1:8801 node shot.mjs   # uses the playwright in ../../../node_modules
```

The `@font-face` URLs point at `paperboat-shared/node_modules` by absolute path; adjust them if
the checkout lives elsewhere. `mockup.css` mirrors the semantic tokens; nothing here is app code.
