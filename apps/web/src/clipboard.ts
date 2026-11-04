// Copy, cut and paste on the Flow canvas: the selected blocks and the wires between them.
// Pure functions over nodes and wires; the keys and the clipboard itself live in FlowEditor.

import type { BlockNode, FlowNode, Wire } from "./FlowEditor";

/** What was copied: blocks (positions on the canvas, as drawn) and the wires joining two of them. */
export type Clip = { nodes: BlockNode[]; edges: Wire[] };

const isBlockNode = (n: FlowNode): n is BlockNode => n.type === "block";

/**
 * The selected blocks with the wires between them. A block that belongs to a station
 * object (`linked`) goes with its object, so it isn't copied; a group is not copied
 * (its blocks are, when selected). A block inside a group is copied at its place on the canvas.
 */
export function copySelection(nodes: FlowNode[], edges: Wire[]): Clip | null {
  const at = new Map(nodes.map((n) => [n.id, n]));
  const picked = nodes.filter((n): n is BlockNode => isBlockNode(n) && !!n.selected && !n.data.def.linked);
  if (picked.length === 0) return null;
  const ids = new Set(picked.map((n) => n.id));
  const placed = picked.map((n) => {
    const parent = n.parentId ? at.get(n.parentId) : undefined;
    const { parentId: _p, extent: _e, selected: _s, dragging: _d, measured: _m, ...rest } = n;
    return { ...rest, position: parent ? { x: parent.position.x + n.position.x, y: parent.position.y + n.position.y } : n.position } as BlockNode;
  });
  return { nodes: placed, edges: edges.filter((w) => ids.has(w.source) && ids.has(w.target)) };
}

/** `type-N` with the lowest N that neither `taken` nor an earlier pick has. */
function freshId(type: string, taken: Set<string>): string {
  let i = 1;
  while (taken.has(`${type}-${i}`)) i++;
  const id = `${type}-${i}`;
  taken.add(id);
  return id;
}

/**
 * Paste `clip` into the canvas: new ids, `shift` px down and right of where it was copied,
 * the pasted blocks selected (the rest not). The wires between them come along.
 */
export function pasteClip(clip: Clip, nodes: FlowNode[], edges: Wire[], shift: number): { nodes: FlowNode[]; edges: Wire[]; ids: string[] } {
  const taken = new Set(nodes.map((n) => n.id));
  const renamed = new Map<string, string>();
  const pasted = clip.nodes.map((n) => {
    const id = freshId(n.data.def.type, taken);
    renamed.set(n.id, id);
    return { ...n, id, selected: true, position: { x: n.position.x + shift, y: n.position.y + shift } } as BlockNode;
  });
  const wires = clip.edges.map((w) => {
    const source = renamed.get(w.source)!;
    const target = renamed.get(w.target)!;
    return { ...w, id: `${source}.${w.sourceHandle}-${target}.${w.targetHandle}`, source, target, selected: false } as Wire;
  });
  return {
    nodes: [...nodes.map((n) => (n.selected ? { ...n, selected: false } : n)), ...pasted],
    edges: [...edges.map((w) => (w.selected ? { ...w, selected: false } : w)), ...wires],
    ids: pasted.map((n) => n.id),
  };
}
