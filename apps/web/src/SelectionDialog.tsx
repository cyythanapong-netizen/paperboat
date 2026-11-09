// The selection dialog (#54): over the blocks and groups picked on the Flow canvas, what can be
// done to them all at once.

import { NodeToolbar, Position } from "@xyflow/react";
import type { Expose, GroupData, GroupNode } from "./groups";
import { isBlock, portsOf, type BlockNode, type FlowNode, type Wire } from "./flowModel";

type Props = {
  pickedNodes: FlowNode[];
  pickedBlocks: BlockNode[];
  pickedWires: Wire[];
  pickedFrames: GroupNode[];
  /** Something is picked, and nothing is being dragged. */
  isVisible: boolean;
  /** Room for the dialog above the selection, else it goes below. */
  roomAbove: boolean;
  allCommented: boolean;
  /** Linked blocks among the picked: they aren't deleted. */
  kept: number;
  canGroup: boolean;
  canFrame: boolean;
  canStory: boolean;
  toggleComment: (mode: "out" | "through") => void;
  removePicked: () => void;
  group: (kind: GroupData["kind"]) => void;
  ungroup: (id: string) => void;
  update: (id: string, change: Partial<GroupData>) => void;
  /** The open subsystem or story, and the one block picked in it: its ports can be exposed on the group. */
  levelGroup?: GroupNode;
  pickedBlock?: BlockNode;
};

export function SelectionDialog({ pickedNodes, pickedBlocks, pickedWires, pickedFrames, isVisible, roomAbove, allCommented, kept, canGroup, canFrame, canStory, toggleComment, removePicked, group, ungroup, update, levelGroup, pickedBlock }: Props) {
  const counted = (n: number, one: string) => (n ? [`${n} ${one}${n === 1 ? "" : "s"}`] : []);
  const summary = [
    ...counted(pickedNodes.filter(isBlock).length, "block"),
    ...counted(pickedFrames.length, "group"),
    ...counted(pickedWires.length, "wire"),
  ].join(" · ");
  return (
    <NodeToolbar
      nodeId={pickedNodes.map((n) => n.id)}
      isVisible={isVisible}
      position={roomAbove ? Position.Top : Position.Bottom}
      offset={14}
      className="selection-dialog nodrag nopan nowheel"
    >
      <span className="lbl">{summary}</span>
      <button
        className="key small"
        disabled={pickedBlocks.length === 0}
        title={allCommented ? "Uncomment selected blocks (Ctrl+/)" : "Comment out selected blocks (Ctrl+/)"}
        onClick={() => toggleComment("out")}
      >
        {allCommented ? "Uncomment" : "Comment"}
      </button>
      <button
        className="key small"
        disabled={pickedBlocks.length === 0}
        title="Bypass (comment through) selected blocks (Ctrl+Shift+Y)"
        onClick={() => toggleComment("through")}
      >
        Bypass
      </button>
      <button
        className="key small"
        disabled={kept === pickedNodes.length && pickedWires.length === 0}
        title="Delete the selection (Delete)"
        onClick={removePicked}
      >
        Delete
      </button>
      <button
        className="key small"
        disabled={!canGroup}
        title={canGroup ? "Make the selection a subsystem: one block here, its insides on their own level (double-click it to open)" : "Pick blocks or groups in the same place (not some in a frame)"}
        onClick={() => group("subsystem")}
      >
        Group
      </button>
      <button
        className="key small"
        disabled={!canStory}
        title={canStory ? "Make the selected blocks a story: one step of a Step flow" : "A story holds signal blocks only: no step ports"}
        onClick={() => group("story")}
      >
        Story
      </button>
      <button className="key small" disabled={!canFrame} title="Draw a frame round the selected blocks (tidy; they stay on this level and run as before)" onClick={() => group("visual")}>
        Frame
      </button>
      <button
        className="key small"
        disabled={pickedFrames.length === 0}
        title="Ungroup: put the selected groups' blocks back on this level"
        onClick={() => pickedFrames.forEach((f) => ungroup(f.id))}
      >
        Ungroup
      </button>
      {levelGroup && pickedBlock && <ExposeMenu block={pickedBlock} group={levelGroup} set={(expose) => update(levelGroup.id, { expose })} />}
      {kept > 0 && (
        <span className="lbl kept" title="Linked blocks come from the backend's own graph">
          {kept} linked {kept === 1 ? "block stays" : "blocks stay"}
        </span>
      )}
    </NodeToolbar>
  );
}

/**
 * A block's ports as checkboxes: a ticked one is a port of the open group (shown on its block
 * from outside) even with no wire yet. Ports with a wire across the group's edge show anyway.
 */
function ExposeMenu({ block, group, set }: { block: BlockNode; group: GroupNode; set: (expose: Expose[]) => void }) {
  const def = portsOf(block);
  const list = group.data.expose ?? [];
  const is = (side: Expose["side"], port: string) => list.some((x) => x.side === side && x.to[0] === block.id && x.to[1] === port);
  const toggle = (side: Expose["side"], port: string) =>
    set(is(side, port) ? list.filter((x) => !(x.side === side && x.to[0] === block.id && x.to[1] === port)) : [...list, { side, to: [block.id, port] }]);
  const rows = [...def.inputs.map((p) => ({ side: "in" as const, id: p.id })), ...def.outputs.map((p) => ({ side: "out" as const, id: p.id }))];
  if (rows.length === 0) return null;
  return (
    <details className="expose-menu">
      <summary className="key small" title="Show a port of this block on the group, even with no wire yet">
        Expose port
      </summary>
      <div className="expose-list">
        {rows.map((r) => (
          <label key={`${r.side}:${r.id}`} className="expose-row">
            <input type="checkbox" checked={is(r.side, r.id)} onChange={() => toggle(r.side, r.id)} />
            <span className="lbl">{r.side === "in" ? "in" : "out"}</span> {r.id}
          </label>
        ))}
      </div>
    </details>
  );
}
