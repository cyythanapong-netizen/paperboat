import { Position, useUpdateNodeInternals } from "@xyflow/react";
import { useEffect, useState, type PointerEvent as ReactPointerEvent, type ReactNode } from "react";
import { movePort, placePorts, resetPort, SIDES, type Placed, type PortSides, type Side } from "./ports";
import type { BlockDef, PortDef } from "./protocol";
import { PortHandle } from "./wireSnap";

const POSITION: Record<Side, Position> = { left: Position.Left, right: Position.Right, top: Position.Top, bottom: Position.Bottom };

/**
 * A block's ports, where the user put them: a strip on top, rows (an input beside an output)
 * and a strip at the bottom, for the card to place round its head and params.
 *
 * A port moves by dragging its name to another side of the block, or along its side. While
 * it is dragged the card shows where it would go; `save` gets the new layout on release (one
 * undo). Double-clicking a name puts that port back. Used by blocks and subsystems alike.
 */
export function usePortLayout(options: {
  /** The node's id (its handles are re-measured when they move). */
  id: string;
  /** Its ports (only `inputs` and `outputs` are read). */
  def: Pick<BlockDef, "inputs" | "outputs">;
  sides: PortSides | undefined;
  save: (sides: PortSides | undefined) => void;
  /** Ports stay put (a missing block). */
  disabled?: boolean;
  /** What a port's row says; default its id. */
  name?: (p: PortDef) => string;
  /** A sign shown after an input's name (a Sum's + or −), if any. */
  sign?: (p: PortDef, dir: "in" | "out") => string;
}): { top: ReactNode; rows: ReactNode; bottom: ReactNode } {
  const { id, sides: saved, save, disabled, name = (p: PortDef) => p.id, sign = () => "" } = options;
  const def = { type: "", label: "", kind: "function", params: [], inputs: options.def.inputs, outputs: options.def.outputs } as BlockDef;
  const updateNodeInternals = useUpdateNodeInternals();
  const [moving, setMoving] = useState<{ id: string; sides: PortSides | undefined } | null>(null);
  const sides = moving ? moving.sides : saved;
  const placed = placePorts(def, sides);
  const key = JSON.stringify(Object.values(placed).map((list) => list.map((e) => e.port.id)));
  useEffect(() => updateNodeInternals(id), [id, key, updateNodeInternals]);

  const startMove = (e: ReactPointerEvent, port: string) => {
    // A press on the dot draws a wire; only the name moves the port.
    if (e.button !== 0 || disabled || (e.target as HTMLElement).closest(".react-flow__handle")) return;
    const card = (e.currentTarget as HTMLElement).closest(".block") as HTMLElement | null;
    if (!card) return;
    const [x0, y0] = [e.clientX, e.clientY];
    let next = saved;
    let live = false;
    const move = (ev: PointerEvent) => {
      if (!live && Math.hypot(ev.clientX - x0, ev.clientY - y0) < 4) return;
      live = true;
      const r = card.getBoundingClientRect();
      // The side nearest the pointer, and the place along it among the other ports there.
      const d: Record<Side, number> = { left: ev.clientX - r.left, right: r.right - ev.clientX, top: ev.clientY - r.top, bottom: r.bottom - ev.clientY };
      const side = SIDES.reduce((a, b) => (d[b] < d[a] ? b : a));
      const across = side === "top" || side === "bottom";
      const index = [...card.querySelectorAll<HTMLElement>(`.port[data-side="${side}"]`)]
        .filter((o) => o.dataset.port !== port)
        .filter((o) => {
          const b = o.getBoundingClientRect();
          return across ? b.left + b.width / 2 < ev.clientX : b.top + b.height / 2 < ev.clientY;
        }).length;
      next = movePort(def, saved, port, side, index);
      setMoving({ id: port, sides: next });
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      window.removeEventListener("pointercancel", up);
      setMoving(null);
      if (live && JSON.stringify(next ?? null) !== JSON.stringify(saved ?? null)) save(next);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    window.addEventListener("pointercancel", up);
  };

  const port = ({ port: p, dir }: Placed, side: Side) => {
    const unit = p.unit && <span className="unit">{side === "right" ? `${p.unit} ` : ` ${p.unit}`}</span>;
    return (
      <div
        key={`${dir}:${p.id}`}
        className={`port ${dir} side-${side} port-${p.type ?? "signal"} nodrag${moving?.id === p.id ? " moving" : ""}`}
        data-side={side}
        data-port={p.id}
        onPointerDown={(e) => startMove(e, p.id)}
        onDoubleClick={(e) => {
          e.stopPropagation();
          if (!disabled) save(resetPort(def, saved, p.id));
        }}
      >
        {side === "right" && unit}
        {name(p)}
        {sign(p, dir) && <span className="port-sign">{sign(p, dir)}</span>}
        {side !== "right" && unit}
        <PortHandle type={dir === "in" ? "target" : "source"} position={POSITION[side]} id={p.id} />
      </div>
    );
  };

  return {
    top: placed.top.length > 0 && <div className="port-strip top">{placed.top.map((e) => port(e, "top"))}</div>,
    // Left and right ports share rows: the first input beside the first output, and so on.
    rows: Array.from({ length: Math.max(placed.left.length, placed.right.length) }, (_, i) => (
      <div key={i} className="port-row">
        {placed.left[i] ? port(placed.left[i], "left") : <span />}
        {placed.right[i] ? port(placed.right[i], "right") : <span />}
      </div>
    )),
    bottom: placed.bottom.length > 0 && <div className="port-strip bottom">{placed.bottom.map((e) => port(e, "bottom"))}</div>,
  };
}
