# pb#312 — Squero Studio Octave, 3D chart mockups

Three design studies for the 3D chart surface in `squero-studio-octave`. They use the real
tokens from `@paperboat/shared/src/tokens.css` and the flat-mono shell of `squero-studio-om`,
not a MATLAB/Octave clone. Each screen is a self-contained HTML page with a canvas height
field (`chart.js`), rendered to JPEG with Playwright at 1600×1000, 1.5×.

| File | Screen | Idea |
| --- | --- | --- |
| `a-bench.jpg` | **Bench** | Workspace-first: variables as live cards, tie a card to a chart axis, scratch line previews its answer before Enter, linked cursor slices. |
| `b-split.jpg` | **Split** | Script-first: inline answer gutter on every assignment, a chart lasso writes a line back into the `.m`, pinned views above the chart. |
| `c-board.jpg` | **Board** | Run-first: 3D chart plus heat, scatter and line as Board instruments, a scrubbable story ribbon, camera keys and ghost compare. |

## The UX ideas

1. **Variable cards, not a table.** Each workspace variable is a card with its shape, type and
   a live glyph: a heatmap for a matrix, a sparkline for a vector, a dot for a scalar. Hovering
   gives the value; no separate variable editor.
2. **Tie, don't configure.** A card's ring handle drags onto a chart axis or a port. The tie is
   a wire in the ecosystem's own language, and the chart says `A tied to z`; rebinding is a
   drag, not a dialog.
3. **Scratch with a preview.** The scratch line evaluates as you type and shows the shape and a
   micro-preview of the result before Enter. `↵ show` pins it; `⇥ send to chart` binds it.
4. **Inline answer gutter.** Every assignment in the editor shows a right-edge chip with its
   shape and a tiny preview. The script is the workspace; no context switch to `whos`.
5. **Lasso → variable.** Lasso a region on the surface or a cluster in scatter, and the app
   proposes the line that produces that selection (`sel = A(A > 0.42)`), with `Insert line` and
   `New chart`. The chart becomes an input to the script.
6. **Time curtain + linked slice.** A vertical plane at the current run time cuts the surface;
   the intersection is drawn in signal orange and mirrored as a 2D profile in the inspector.
   Dragging the plane scrubs the run.
7. **Pinned views (plot shelf).** A pin stores the camera, the curtain and the visible series,
   not just a screenshot. Flip between `iso`, `top` and `sel` like a storyboard; `+ pin view`
   adds one.
8. **Ghost compare.** A previous run is a translucent wireframe behind the current surface, with
   a delta readout. Toggle it from the tile header, not a global menu.
9. **Charts as Board instruments.** The 3D chart is a tile on the Board: drag, resize, tab,
   section, like every other Paperboat instrument. No floating figure windows.
10. **Camera keys and stories.** Saved viewpoints sit next to the grid; the ribbon marks
    `start`, `fold`, `pin` and jumps the run when clicked. Same scrubber language as the
    MultiBody viewport in Squero Studio OM.

## Regenerate

```sh
cd docs/mockups/312
python3 -m http.server 8801 --bind 127.0.0.1
BASE=http://127.0.0.1:8801 node shot.mjs   # uses the playwright in ../../../node_modules
```

The `@font-face` URLs point at `paperboat-shared/node_modules` by absolute path; adjust them if
the checkout lives elsewhere. `mockup.css` mirrors the semantic tokens; nothing here is app code.
