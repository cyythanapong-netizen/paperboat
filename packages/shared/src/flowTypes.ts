import type { Node, Edge } from "@xyflow/react";
import type { BlockDef, ParamValue, PortDef, PortSides } from "./protocol.ts";

export type BlockData = {
  def: BlockDef;
  params: Record<string, ParamValue>;
  missing?: boolean;
  ports?: PortSides;
  commented?: boolean;
  commentMode?: "out" | "through";
  comment?: string;
};

export type Expose = { side: "in" | "out"; to: [string, string] };

export type GroupData = {
  kind: "visual" | "subsystem" | "story";
  label: string;
  /** A frame drawn as one block (subsystems and stories always are, from outside). */
  collapsed: boolean;
  seconds: number;
  /** A story's blocks start afresh each time a path arrives. */
  reset: boolean;
  /** A story's outputs to clear when its path leaves. */
  release?: [string, string][];
  /** A frame's size when open. */
  size: { w: number; h: number };
  /** Ports shown as a block's even with no wire yet: an input or output inside it. */
  expose?: Expose[];
  /** Where the user put its ports (as a block's `ports`). */
  sides?: PortSides;
  /** Filled for display: the ports it shows as a block (`node::port` ids). */
  ports?: { inputs: PortDef[]; outputs: PortDef[] };
};

export type BlockNode = Node<BlockData, "block">;
export type GroupNode = Node<GroupData, "frame">;
export type FlowNode = BlockNode | GroupNode;
export type Wire = Edge<{ type: string }, "wire">;
