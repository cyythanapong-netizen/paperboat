// Copy, cut and paste on the Flow canvas: the selected blocks and groups, and the wires between them.
// Pure functions over nodes and wires; the keys and the clipboard itself live in FlowEditor.

import type { BlockNode, FlowNode, GroupNode, Wire } from "./flowTypes.ts";

type Item = BlockNode | GroupNode;

/** What was copied: blocks and groups (a group with everything in it), and the wires joining two of them. */
export type Clip = { nodes: Item[]; edges: Wire[] };

// Not imported from groups.tsx: this file stays plain TypeScript, so the tests can run it as it is.
const isFrame = (n: FlowNode): n is GroupNode => n.type === "frame";
/** Where a node is on the canvas, through every group it is in. */
function absolute(n: FlowNode, at: Map<string, FlowNode>): { x: number; y: number } {
  let { x, y } = n.position;
  for (let p = n.parentId ? at.get(n.parentId) : undefined, guard = 0; p && guard < 64; p = p.parentId ? at.get(p.parentId) : undefined, guard++) {
    x += p.position.x;
    y += p.position.y;
  }
  return { x, y };
}
const isBlockNode = (n: FlowNode): n is BlockNode => n.type === "block";
const copyable = (n: FlowNode): n is Item => (isBlockNode(n) && !n.data.def.linked) || isFrame(n);

/**
 * The selected blocks and groups with the wires between them. A group takes everything in it
 * (groups in groups too). A block that belongs to a station object (`linked`) goes with its
 * object, so it isn't copied. Whatever isn't inside a copied group is copied at its place on
 * the canvas; what is inside keeps its place in the group.
 */
export function copySelection(nodes: FlowNode[], edges: Wire[]): Clip | null {
  const at = new Map(nodes.map((n) => [n.id, n]));
  const inPicked = (n: FlowNode): boolean => {
    for (let p = n.parentId, guard = 0; p && guard < 64; p = at.get(p)?.parentId, guard++) {
      const g = at.get(p);
      if (g && isFrame(g) && g.selected) return true;
    }
    return false;
  };
  const picked = nodes.filter((n): n is Item => copyable(n) && (!!n.selected || inPicked(n)));
  if (picked.length === 0) return null;
  const ids = new Set(picked.map((n) => n.id));
  const placed = picked.map((n) => {
    const { extent: _e, selected: _s, dragging: _d, measured: _m, ...rest } = n;
    if (n.parentId && ids.has(n.parentId)) return rest as Item;
    const { parentId: _p, ...top } = rest;
    return { ...top, position: absolute(n, at) } as Item;
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
 * the pasted blocks and groups (not what is inside a group) selected, the rest not. The wires
 * between them come along, and a group keeps its blocks.
 */
export function pasteClip(clip: Clip, nodes: FlowNode[], edges: Wire[], shift: number): { nodes: FlowNode[]; edges: Wire[]; ids: string[] } {
  const taken = new Set(nodes.map((n) => n.id));
  const renamed = new Map<string, string>();
  for (const n of clip.nodes) renamed.set(n.id, freshId(isFrame(n) ? (n.data.kind === "visual" ? "frame" : n.data.kind) : n.data.def.type, taken));
  const pasted = clip.nodes.map((n) => {
    const inside = !!n.parentId && renamed.has(n.parentId);
    const moved = {
      ...n,
      id: renamed.get(n.id)!,
      ...(inside ? { parentId: renamed.get(n.parentId!) } : {}),
      selected: !inside,
      ...(inside ? {} : { position: { x: n.position.x + shift, y: n.position.y + shift } }),
    };
    if (!isFrame(n)) return moved as Item;
    // A group's released outputs and exposed ports name blocks: the ones in the copy follow their new ids.
    const follow = ([b, p]: [string, string]): [string, string] => [renamed.get(b) ?? b, p];
    const data = { ...n.data };
    if (data.release) data.release = data.release.map(follow);
    if (data.expose) data.expose = data.expose.map((x) => ({ ...x, to: follow(x.to) }));
    return { ...moved, data } as Item;
  });
  const wires = clip.edges.map((w) => {
    const source = renamed.get(w.source)!;
    const target = renamed.get(w.target)!;
    return { ...w, id: `${source}.${w.sourceHandle}-${target}.${w.targetHandle}`, source, target, selected: false } as Wire;
  });
  return {
    nodes: [...nodes.map((n) => (n.selected ? { ...n, selected: false } : n)), ...pasted],
    edges: [...edges.map((w) => (w.selected ? { ...w, selected: false } : w)), ...wires],
    ids: pasted.filter((n) => n.selected).map((n) => n.id),
  };
}
