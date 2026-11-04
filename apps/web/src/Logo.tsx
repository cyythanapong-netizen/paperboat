/**
 * The Paperboat mark (apps/web/DESIGN.md, "Logo"): a two-tone paper boat in pixels, over
 * a row of larger water pixels. The boat is 31 columns of 2-unit pixels; the water is
 * five 6-unit pixels, so the mark is 62 × 42 units. Drawn from tokens, so it follows the
 * theme; the favicon (public/favicon.svg) is the same geometry in the brand colours.
 */
const run = (x: number, y: number, w: number, h: number) => `M${x} ${y}h${w}v${h}h-${w}z`;

export const LOGO_SIZE = { width: 62, height: 42 };

/** Path data for each part of the mark. */
export function logoPaths() {
  let light = "";
  let shade = "";
  let hull = "";
  let water = "";
  for (let r = 0; r < 8; r++) {
    light += run((15 - r) * 2, r * 2, (r + 1) * 2, 2);
    if (r > 0) shade += run(32, r * 2, r * 2, 2);
  }
  [31, 27, 25, 23, 21, 19, 17].forEach((cols, i) => {
    hull += run(31 - cols, (9 + i) * 2, cols * 2, 2);
  });
  for (let k = 0; k < 5; k++) water += run(k * 14, 36, 6, 6);
  return { light, shade, hull, water };
}

const PATHS = logoPaths();

/** The mark alone; `height` in CSS pixels. */
export function Logo({ height = 24 }: { height?: number }) {
  const width = (height * LOGO_SIZE.width) / LOGO_SIZE.height;
  return (
    <svg className="logo" width={width} height={height} viewBox={`0 0 ${LOGO_SIZE.width} ${LOGO_SIZE.height}`} shapeRendering="crispEdges" aria-hidden="true">
      <path className="logo-light" d={PATHS.light} />
      <path className="logo-shade" d={PATHS.shade} />
      <path className="logo-light" d={PATHS.hull} />
      <path className="logo-water" d={PATHS.water} />
    </svg>
  );
}
