import { Logo } from "./Logo";

/**
 * Loading spinner using the Paperboat water ripple (from Logo.tsx / DESIGN.md).
 * Uses the five signal-coloured water dots that ripple in sequence, or the mini boat.
 */
const WATER_XS = [0, 14, 28, 42, 56];

export interface SpinnerProps {
  label?: string;
  size?: number;
  variant?: "ripple" | "boat";
}

export function Spinner({ label, size = 12, variant = "ripple" }: SpinnerProps) {
  if (variant === "boat") {
    return (
      <span className="spinner spinner-boat" role="status" aria-label={label ?? "Loading"}>
        <Logo height={size} running={true} />
      </span>
    );
  }

  // Width is proportional to the 5 water dots (62x18 viewBox, ~4:1 ratio)
  const width = Math.round((size * 56) / 14);

  return (
    <span className="spinner spinner-ripple logo-live" role="status" aria-label={label ?? "Loading"}>
      <svg
        viewBox="0 24 62 18"
        width={width}
        height={size}
        style={{ ["--lr" as any]: "3.5px" }}
        shapeRendering="crispEdges"
        aria-hidden="true"
      >
        <g className="logo-water">
          {WATER_XS.map((x, i) => (
            <g key={i} className={`w w${i + 1}`}>
              <rect x={x} y={32} width={6} height={6} />
            </g>
          ))}
        </g>
      </svg>
    </span>
  );
}

