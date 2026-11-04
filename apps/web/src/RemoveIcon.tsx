/**
 * The × of a round delete button, drawn as two strokes instead of a font glyph. A glyph is
 * centred on its font's line box, not on the visible mark, so it drifts with every font or
 * size change; the strokes are centred exactly, at any zoom, and take the button's colour.
 */
export function RemoveIcon() {
  return (
    <svg className="remove-icon" viewBox="0 0 10 10" aria-hidden="true">
      <path d="M2 2 L8 8 M8 2 L2 8" />
    </svg>
  );
}
