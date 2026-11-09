// What the Flow canvas tells its blocks and wires: shared through context, so a node or an edge
// redraws from it without the canvas passing props through React Flow.

import { createContext } from "react";
import type { Connectors } from "./protocol";
import type { WirePoint, WireRoute } from "./wireRouting.ts";

/** What the last run said about blocks (`node.state`): where a Step flow is, which failed. */
export const RunStates = createContext<{
  active: ReadonlySet<string>;
  failed: ReadonlySet<string>;
  /** Waiting their turn in the run (queued). */
  queued: ReadonlySet<string>;
  /** Reused from an earlier run (cached). */
  cached: ReadonlySet<string>;
  /** How far a block's long step has got (node.progress). */
  progress: Readonly<Record<string, number>>;
}>({
  active: new Set(),
  failed: new Set(),
  queued: new Set(),
  cached: new Set(),
  progress: {},
});

/** What the canvas knows of icon ports: the catalog's connectors, wires per port, ports whose wires dangle (`if` false). */
export const ConnectorsContext = createContext<Connectors>({});
export const IconWires = createContext<{ count: ReadonlyMap<string, Record<string, number>>; missing: ReadonlySet<string> }>({ count: new Map(), missing: new Set() });

/** The wire a dragged card would be inserted into, if any (drawn thicker). */
export const HotWire = createContext<string | null>(null);
/** The wires to follow (#55): those of the hovered wire, port or block. null: nothing is hovered. */
export const Lit = createContext<ReadonlySet<string> | null>(null);
/** Whole-canvas routed wire geometries computed in one pass. */
export const WireRoutes = createContext<Map<string, WireRoute> | null>(null);
export const CommentedOutContext = createContext<ReadonlySet<string>>(new Set());
/** Save a wire's dragged bends; null when the canvas is locked (wires don't move). */
export const SetBends = createContext<((id: string, bends: WirePoint[]) => void) | null>(null);
