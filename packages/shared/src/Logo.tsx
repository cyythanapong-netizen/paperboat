/**
 * The Paperboat mark (apps/web/DESIGN.md, "Logo"): a two-tone paper boat in pixels, over
 * a row of larger water pixels. The boat is 31 columns of 2-unit pixels; the water is
 * five 6-unit pixels, so the mark is 62 × 42 units.
 *
 * Below ~32px the 2-unit pixels stop reading, so the mark switches to a smooth geometric
 * variant (S2): a two-tone triangle sail + a trapezoid hull, keeping the original pixel
 * water dots. At < 20px the sail and hull merge (no gap) for legibility.
 *
 * The water is the signal colour and means "live". When `running` is true the water ripples
 * (W2) and the whole mark bobs once; otherwise an idle ripple passes every ~18s. Motion
 * respects prefers-reduced-motion (tokens drop to 0).
 *
 * Drawn from tokens, so it follows the theme; the favicon (public/favicon.svg) is the same
 * geometry in the brand colours and stays pixel art.
 */
const run = (x: number, y: number, w: number, h: number) => `M${x} ${y}h${w}v${h}h-${w}z`;

export const LOGO_SIZE = { width: 62, height: 42 };

const WATER_XS = [0, 14, 28, 42, 56];

/** A block of water dots, each wrapped so the ripple can lift them in sequence. */
export function Logo({ height = 24, running = false }: { height?: number; running?: boolean }) {
  const width = (height * LOGO_SIZE.width) / LOGO_SIZE.height;
  const variant: "pixel" | "s2" | "s2-merged" =
    height >= 32 ? "pixel" : height >= 20 ? "s2" : "s2-merged";

  // Ripple amplitude scales with the mark so small marks stay subtle.
  // W2: wave 25% higher (3.0 → 3.75), boat bob 1.0 (was 1.5). Made a touch taller so
  // the crest reads at the bar size.
  const lr = height >= 32 ? "4.5px" : height >= 20 ? "2.4px" : "1.8px";
  const lb = height >= 32 ? "1.5px" : height >= 20 ? "0.8px" : "0.5px";

  const cls = `${running ? "logo-live" : "logo-idle"}`;

  if (variant === "pixel") {
    let light = "", shade = "", hull = "";
    for (let r = 0; r < 8; r++) {
      light += run((15 - r) * 2, r * 2, (r + 1) * 2, 2);
      if (r > 0) shade += run(32, r * 2, r * 2, 2);
    }
    [31, 27, 25, 23, 21, 19, 17].forEach((cols, i) => {
      hull += run(31 - cols, (9 + i) * 2, cols * 2, 2);
    });
    return (
      <svg
        className={`logo ${cls}`}
        width={width}
        height={height}
        viewBox={`0 0 ${LOGO_SIZE.width} ${LOGO_SIZE.height}`}
        shapeRendering="crispEdges"
        aria-hidden="true"
        style={{ ["--lr" as any]: lr, ["--lb" as any]: lb }}
      >
        <path className="logo-light" d={light} />
        <path className="logo-shade" d={shade} />
        <path className="logo-light" d={hull} />
        <g className="logo-water" shapeRendering="crispEdges">
          {WATER_XS.map((x, i) => (
            <g key={i} className={`w w${i + 1}`}>
              <rect x={x} y={36} width={6} height={6} />
            </g>
          ))}
        </g>
      </svg>
    );
  }

  const micro = variant === "s2-merged";
  const leftSail = micro ? "M31 0 L31 15.5 L14 15.5 Z" : "M31 0 L31 16 L15 16 Z";
  const rightSail = micro ? "M31 0 L48 15.5 L31 15.5 Z" : "M31 0 L47 16 L31 16 Z";
  const hull = micro
    ? "M0 19 L62 19 L48 31.5 L14 31.5 Z"
    : "M0 18.2 L62 18.2 L48 32 L14 32 Z";
  const waterY = micro ? 35.6 : 36;
  const waterSize = micro ? 6.4 : 6;

  return (
    <svg
      className={`logo ${cls}`}
      width={width}
      height={height}
      viewBox={`0 0 ${LOGO_SIZE.width} ${LOGO_SIZE.height}`}
      shapeRendering="geometricPrecision"
      aria-hidden="true"
      style={{ ["--lr" as any]: lr, ["--lb" as any]: lb }}
    >
      <path className="logo-light" d={leftSail} />
      <path className="logo-shade" d={rightSail} />
      <path className="logo-hull" d={hull} />
      <g className="logo-water" shapeRendering={micro ? "geometricPrecision" : "crispEdges"}>
        {WATER_XS.map((x, i) => (
          <g key={i} className={`w w${i + 1}`}>
            <rect x={x} y={waterY} width={waterSize} height={waterSize} />
          </g>
        ))}
      </g>
    </svg>
  );
}
