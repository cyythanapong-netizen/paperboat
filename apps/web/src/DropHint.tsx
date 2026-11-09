import { BLOCK_SIZE, type Hint } from "./cardDrop";

/** What dropping the dragged card would do: an outline where it goes, or a line to the port it would join. */
export function DropHint({ hint, rel }: { hint: Hint; rel: (x: number, y: number) => { x: number; y: number } }) {
  return (
    <div className="drop-hint" aria-live="polite">
      {hint.plan.kind === "place" && (
        <div className="drop-outline" style={{ left: rel(hint.x, hint.y).x - BLOCK_SIZE.w / 2, top: rel(hint.x, hint.y).y - 16 }} />
      )}
      {hint.plan.kind === "port" && (
        <svg className="drop-wire">
          <line x1={rel(hint.plan.hx, hint.plan.hy).x} y1={rel(hint.plan.hx, hint.plan.hy).y} x2={rel(hint.x, hint.y).x} y2={rel(hint.x, hint.y).y} />
          <circle cx={rel(hint.plan.hx, hint.plan.hy).x} cy={rel(hint.plan.hx, hint.plan.hy).y} r={5} />
        </svg>
      )}
      <span className="drop-label" style={{ left: rel(hint.x, hint.y).x + 14, top: rel(hint.x, hint.y).y + 18 }}>
        {hint.label}
      </span>
    </div>
  );
}
