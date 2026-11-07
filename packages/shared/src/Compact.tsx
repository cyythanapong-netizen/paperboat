import { Position } from "@xyflow/react";
import { Fragment, useEffect, useRef, useState, type ReactNode } from "react";
import { isRef } from "./bindings.ts";
import { Icon } from "./Icon.tsx";
import { iconValid, plateSize } from "./icon.ts";
import { connectorHint, placeIconPorts, portHint, portLabel, signOf } from "./ports.ts";
import type { BlockDef, Connectors, IconDef, ParamValue } from "./protocol.ts";
import { PortHandle } from "./wireSnap.tsx";

/** Curves a glyph can name: the block's transfer in a few strokes. Anything else is drawn as text. */
const CURVES: Record<string, ReactNode> = {
  gain: (
    <>
      <path className="ax" d="M3 14H27" />
      <path d="M5 24L25 6" />
    </>
  ),
  plus: <path d="M15 6V24M6 15H24" />,
  step: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M4 21H15V8H26" />
    </>
  ),
  clamp: (
    <>
      <path className="ax" d="M3 15H27" />
      <path d="M4 22H9L21 8H26" />
    </>
  ),
  const: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M4 12H26" />
    </>
  ),
  abs: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M5 8L15 22L25 8" />
    </>
  ),
  staircase: (
    <>
      <path className="ax" d="M3 15H27" />
      <path d="M4 23H11V16H18V9H25" />
    </>
  ),
  window: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M4 22H9V10H21V22H26" />
    </>
  ),
  pulse: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M4 22H8V10H14V22H18V10H24V22H26" />
    </>
  ),
  timer: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M4 22H17V9H26" />
      <path className="ax" strokeDasharray="2 2" d="M17 9V24" />
    </>
  ),
  ramp: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M5 21L25 7" />
      <circle cx="5" cy="21" r="1.5" />
      <circle cx="25" cy="7" r="1.5" />
    </>
  ),
  lerp: (
    <>
      <path className="ax" d="M3 24H27" />
      <path d="M5 20L25 8" />
      <circle cx="15" cy="14" r="2" fill="currentColor" />
    </>
  ),
};

export function Glyph({ name }: { name: string }) {
  return (
    <svg className="glyph" viewBox="0 0 30 30" aria-hidden="true">
      {CURVES[name] ?? (
        <text x="15" y="21" textAnchor="middle">
          {name}
        </text>
      )}
    </svg>
  );
}

/** A param as the caption shows it: a bound one by its variable's name or formula (#36). */
const show = (v: ParamValue | undefined) =>
  isRef(v) ? ("var" in v ? v.var : `ƒ ${v.expr}`) : typeof v === "number" ? String(+v.toFixed(4)).replace("-", "−") : typeof v === "boolean" ? (v ? "on" : "off") : String(v ?? "");

/** The caption's value: `{k}` and `{min}…{max}` with the block's params filled in. */
export function captionValue(template: string | undefined, params: Record<string, ParamValue>): string {
  return template ? template.replace(/\{(\w+)\}/g, (_, id: string) => show(params[id])) : "";
}

/**
 * A simple block as a small plate (#70): a glyph in a 64×56 box, bare port dots (a port's
 * name shows on hover), and the block's name and main value as a caption. Click opens its
 * params in a popover; double-click unfolds it into the full card (`onExpand`).
 * `word` is what is happening to it (running, failed), said in words and in the outline.
 */
export function CompactBlock({
  id,
  def,
  params,
  selected,
  commented,
  word,
  progress,
  onExpand,
  popover,
  connectors,
  wires,
}: {
  id: string;
  def: BlockDef;
  params: Record<string, ParamValue>;
  selected: boolean;
  commented?: boolean;
  word?: "running" | "failed";
  progress?: number;
  onExpand: () => void;
  /** The param rows shown in the popover. */
  popover: ReactNode;
  /** The catalog's connector classes, for `view.icon.ports`. */
  connectors?: Connectors;
  /** How many wires meet at each icon port (3 or more draw a junction dot). */
  wires?: Record<string, number>;
}) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!selected) setOpen(false);
  }, [selected]);
  useEffect(() => {
    if (!open) return;
    const away = (e: PointerEvent) => !root.current?.contains(e.target as globalThis.Node | null) && setOpen(false);
    const key = (e: KeyboardEvent) => e.key === "Escape" && setOpen(false);
    document.addEventListener("pointerdown", away);
    document.addEventListener("keydown", key);
    return () => {
      document.removeEventListener("pointerdown", away);
      document.removeEventListener("keydown", key);
    };
  }, [open]);

  const value = captionValue(def.view?.value, params);
  const glyphName = def.view?.glyph ? captionValue(def.view.glyph, params) : "const";
  const spread = (n: number, i: number) => `${((i + 1) / (n + 1)) * 100}%`;
  const hasParams = def.params.length > 0;
  const icon = iconValid(def.view?.icon) ? def.view?.icon : undefined; // else the plain plate
  const size = icon ? plateSize(icon) : undefined;
  return (
    <div
      ref={root}
      className={["compact-block", `kind-${def.kind}`, selected && "selected", commented && "commented", word && word, open && "open"].filter(Boolean).join(" ")}
      onDoubleClick={onExpand}
    >
      <div
        className="plate"
        role="button"
        tabIndex={0}
        aria-expanded={hasParams ? open : undefined}
        aria-label={`${def.label}${value ? ` ${value}` : ""}`}
        style={size && { width: size.w, height: size.h }}
        title={hasParams ? "Click for its params; double-click for the full block" : "Double-click for the full block"}
        onClick={() => hasParams && setOpen(!open)}
        onKeyDown={(e) => {
          if (e.target !== e.currentTarget) return;
          if ((e.key === "Enter" || e.key === " ") && hasParams) setOpen(!open);
        }}
      >
        {icon ? <Icon icon={icon} type={def.type} fill={(t) => captionValue(t, params)} /> : <Glyph name={glyphName} />}
        {icon ? (
          <IconPorts icon={icon} params={params} connectors={connectors} wires={wires} />
        ) : (
          <>
            {def.inputs.map((p, i) => (
              <PortHandle key={p.id} type="target" position={Position.Left} id={p.id} top={spread(def.inputs.length, i)} label={portLabel(p)} title={portHint(p)} className={`t-${p.type}`} />
            ))}
            {def.inputs.map((p, i) => signOf(def, params, i) && (
              <span key={`sign-${p.id}`} className="plate-sign" style={{ top: spread(def.inputs.length, i) }} aria-hidden="true">
                {signOf(def, params, i)}
              </span>
            ))}
            {def.outputs.map((p, i) => (
              <PortHandle key={p.id} type="source" position={Position.Right} id={p.id} top={spread(def.outputs.length, i)} label={portLabel(p)} title={portHint(p)} className={`t-${p.type}`} />
            ))}
          </>
        )}
        {word === "running" && progress !== undefined && progress < 1 && <i className="plate-prog" style={{ width: `${progress * 100}%` }} />}
      </div>
      <div className="plate-cap">
        <span className={word ? `cap-${word}` : undefined}>{word ?? def.label}</span>
        {value && <span className="plate-val">{value}</span>}
      </div>
      {open && (
        <div className="popover nodrag nowheel" onDoubleClick={(e) => e.stopPropagation()} onPointerDown={(e) => e.stopPropagation()}>
          <div className="popover-title">
            {def.label}
            <span>{id}</span>
          </div>
          {popover}
        </div>
      )}
    </div>
  );
}

const SIDE_POSITION = { left: Position.Left, right: Position.Right, top: Position.Top, bottom: Position.Bottom } as const;

/**
 * An icon's connector ports (§4): a handle per port in the connector's shape and size, at `side`
 * and `at`. An acausal port has no direction, so it carries a source and a target handle in one
 * place and a wire starts or ends there either way. A port hidden by its `if` keeps an invisible
 * handle so its wires stay. 3 or more wires on a port draw a 4px junction dot.
 */
function IconPorts({ icon, params, connectors, wires }: { icon: IconDef; params: Record<string, ParamValue>; connectors?: Connectors; wires?: Record<string, number> }) {
  return (
    <>
      {placeIconPorts(icon, params).map(({ port, at, hidden, count }) => {
        const c = connectors?.[port.class];
        const along = port.side === "left" || port.side === "right" ? { top: `${at}%` } : { left: `${at}%` };
        const fill = (port.variant ?? c?.alt) === "filled" ? "filled" : "hollow"; // a port's own variant wins over the class's look
        const look = `ip ip-${c?.handle ?? "circle"} ip-${c?.size ?? 8} ip-${fill} k-${c?.kind ?? "output"}${hidden ? " ip-hidden" : ""}`;
        const title = connectorHint(port.id, c);
        const kinds: ("source" | "target")[] = !c || c.kind === "acausal" ? ["source", "target"] : c.kind === "output" ? ["source"] : ["target"];
        return (
          <Fragment key={port.id}>
            {kinds.map((type) => (
              <PortHandle key={type} type={type} position={SIDE_POSITION[port.side]} id={port.id} {...along} label={hidden ? undefined : port.id} title={title} className={look} />
            ))}
            {count !== undefined && !hidden && (
              <span className={`ip-count side-${port.side}`} style={along} aria-hidden="true">
                {count}
              </span>
            )}
            {(wires?.[port.id] ?? 0) >= 3 && <i className="ip-junction" style={along} data-side={port.side} aria-hidden="true" />}
          </Fragment>
        );
      })}
    </>
  );
}
