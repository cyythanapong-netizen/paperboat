import { BaseEdge, EdgeLabelRenderer, Position, useReactFlow, type EdgeProps } from "@xyflow/react";
import { useContext } from "react";
import { useWireBends } from "@paperboat/shared/components";
import { RemoveIcon } from "./RemoveIcon";
import { middleOfWire, orthoPoints, roundedPolyline, squareUp, type WirePoint } from "./wireRouting.ts";
import { CommentedOutContext, HotWire, IconWires, Lit, ProbeValues, SetBends, WireRoutes } from "./flowContexts";
import type { Wire } from "./flowModel";

const sideOf = (p: Position): "left" | "right" | "top" | "bottom" => (p === Position.Left ? "left" : p === Position.Top ? "top" : p === Position.Bottom ? "bottom" : "right");

/**
 * The arrowhead of a step wire, as a filled triangle at its target end. React Flow's own
 * marker sits under the target node, so it reads as another plain line; this one stays
 * just outside the port and says which way control passes.
 */
function stepArrow(points: readonly WirePoint[]): string | null {
  if (points.length < 2) return null;
  const [a, b] = [points[points.length - 2], points[points.length - 1]];
  const [dx, dy] = [b[0] - a[0], b[1] - a[1]];
  const length = Math.hypot(dx, dy) || 1;
  const [ux, uy] = [dx / length, dy / length];
  const size = 9;
  const half = 4;
  const [bx, by] = [b[0] - ux * size, b[1] - uy * size];
  return `${b[0]},${b[1]} ${bx - uy * half},${by + ux * half} ${bx + uy * half},${by - ux * half}`;
}

export function WireEdge({ id, data, selected, markerEnd, ...geometry }: EdgeProps<Wire>) {
  const { deleteElements } = useReactFlow();
  const hot = useContext(HotWire) === id;
  const lit = useContext(Lit);
  const follow = lit ? (lit.has(id) ? " lit" : " dim") : "";
  const routes = useContext(WireRoutes);
  const route = routes?.get(id);
  const commentedOut = useContext(CommentedOutContext);
  const wireCommented = commentedOut.has(geometry.source) || commentedOut.has(geometry.target);
  const missing = useContext(IconWires).missing;
  // A wire into a monitor reads the value it carried at the replay playhead (#284).
  const probes = useContext(ProbeValues);
  const probe = probes && geometry.targetHandleId ? probes.get(`${geometry.target}:${geometry.targetHandleId}`) : undefined;
  const commentedClass = (wireCommented ? " wire-commented" : "") + (missing.has(`${geometry.source}:${geometry.sourceHandleId}`) || missing.has(`${geometry.target}:${geometry.targetHandleId}`) ? " wire-missing" : "");

  const setBends = useContext(SetBends);
  // The ports as the router saw them, else as React Flow does.
  const from: WirePoint = route ? route.points[0] : [geometry.sourceX, geometry.sourceY];
  const to: WirePoint = route ? route.points[route.points.length - 1] : [geometry.targetX, geometry.targetY];
  const routed = data?.points
    ? squareUp(from, data.points, to)
    : (route?.points ?? orthoPoints(from, sideOf(geometry.sourcePosition), to, sideOf(geometry.targetPosition)));
  const { points, handles } = useWireBends(routed, !!selected && !!setBends, (bends) => setBends?.(id, bends));
  const path = roundedPolyline(points, 8);
  const arrow = data?.type === "step" ? stepArrow(points) : null;
  const [x, y] = route && !data?.points && points === routed ? [route.labelX, route.labelY] : middleOfWire(points);
  return (
    <>
      {/* Casing: a wider stroke in the canvas colour, so where wires cross, one is seen to pass over the other. */}
      <path d={path} className={`wire-casing${follow}${commentedClass}`} />
      <BaseEdge id={id} path={path} markerEnd={markerEnd} className={`wire-${data?.type ?? "signal"}${hot ? " drop-target" : ""}${follow}${commentedClass}`} />
      {arrow && <polygon className={`wire-arrow${follow}${commentedClass}`} points={arrow} />}
      {/* What a dropped card finds under the pointer: a wide, invisible stroke tagged with the wire. */}
      <path d={path} className="wire-hit" data-wire={id} />
      {probe !== undefined && (
        <EdgeLabelRenderer>
          <span className="wire-probe nodrag nopan" title="Value on this wire at the replay time" style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y + 10}px)` }}>
            {probe}
          </span>
        </EdgeLabelRenderer>
      )}
      {selected && (
        <EdgeLabelRenderer>
          <button
            className="remove nodrag nopan"
            title="Delete wire"
            style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y - 16}px)` }}
            onClick={() => deleteElements({ edges: [{ id }] })}
          >
            <RemoveIcon />
          </button>
        </EdgeLabelRenderer>
      )}
      {handles}
    </>
  );
}
