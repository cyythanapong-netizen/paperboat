import { createElement } from "react";
import { iconNodes, plateSize } from "./icon.ts";
import type { IconDef } from "./protocol.ts";

const camel = (k: string) => k.replace(/-(\w)/g, (_, c: string) => c.toUpperCase()); // stroke-width -> strokeWidth

/** A block's `view.icon` as an SVG sized to its plate; null when it cannot be drawn (the caller draws the plain plate). */
export function Icon({ icon, type, fill }: { icon: IconDef | undefined; type?: string; fill?: (s: string) => string }) {
  const nodes = iconNodes(icon, type, fill);
  if (!nodes || !icon) return null;
  const { w, h } = plateSize(icon);
  return (
    <svg className="icon-art" width={w} height={h} viewBox={`0 0 ${w} ${h}`} aria-hidden="true">
      {nodes.map((n, i) => createElement(n.tag, { key: i, ...Object.fromEntries(Object.entries(n.attrs).map(([k, v]) => [camel(k), v])) }, n.text))}
    </svg>
  );
}
