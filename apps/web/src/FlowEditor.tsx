import { Background, MiniMap, ReactFlow, SelectionMode, useReactFlow, useStore, useStoreApi, useUpdateNodeInternals, type OnEdgesChange, type OnNodesChange } from "@xyflow/react";
import { useContext, useEffect, useMemo, useRef, useState, type Dispatch, type MutableRefObject, type SetStateAction } from "react";
import { QuickAdd, type Item } from "./Browser";
import { copySelection } from "./clipboard";
import { useCanvasHotkeys } from "@paperboat/shared";
import { CanvasHelp } from "@paperboat/shared/components";
import { SNAP, WireDragContext } from "./wireSnap";
import { arrangeFlowNodes, canArrangeFlow } from "./flowLayout";
import type { WirePoint } from "./wireRouting.ts";
import { Boundary, display, GroupActions, GroupBox, isFrame, type GroupData, type GroupNode } from "./groups";
import type { BlockDef, Connectors } from "./protocol";
import { isBlock, portType, type FlowNode, type Wire } from "./flowModel";
import { withBlock } from "./flowGraph";
import { CommentedOutContext, ConnectorsContext, HotWire, IconWires, Lit, RunStates, SetBends, WireRoutes } from "./flowContexts";
import { commentedOutIds, iconWireCounts, routeCanvasWires } from "./flowWires";
import { wireRules } from "./wireRules";
import { useFlowLevels } from "./flowLevels";
import { useFlowGroups } from "./flowGroups";
import { useLitWires } from "./litWires";
import { useFlowClipboard } from "./flowClipboard";
import { useWireDrag } from "./wireDrag";
import { BLOCK_SIZE, useCardDrop } from "./cardDrop";
import { flipBlock, rotateBlock } from "./blockEdits";
import { Block } from "./Block";
import { WireEdge } from "./WireEdge";
import { LevelCrumbs } from "./LevelCrumbs";
import { SelectionDialog } from "./SelectionDialog";
import { DropHint } from "./DropHint";

const nodeTypes = { block: Block, frame: GroupBox, boundary: Boundary };
const edgeTypes = { wire: WireEdge };

type Props = {
  blocks: BlockDef[];
  /** The catalog's connector classes (icon ports, §4). */
  connectors?: Connectors;
  nodes: FlowNode[];
  edges: Wire[];
  setNodes: Dispatch<SetStateAction<FlowNode[]>>;
  setEdges: Dispatch<SetStateAction<Wire[]>>;
  onNodesChange: OnNodesChange<FlowNode>;
  onEdgesChange: OnEdgesChange<Wire>;
  /** Show the overview map: when the Flow page is the one in view, not among all pages (Commander). */
  minimap: boolean;
  /** The subsystem or story open on the canvas (#68); null: the top level. */
  level: string | null;
  setLevel: (level: string | null) => void;
  /** A run locks arranging: the station and its linked blocks must stay put. */
  locked: boolean;
  /** Filled with what the menus call: the same actions as the keys and the selection dialog. */
  controls?: MutableRefObject<FlowControls | null>;
};

/**
 * The canvas's actions for the menus (#78). `can` says what the selection allows now; it is read
 * when a menu opens, so `paste` looks at the clipboard then.
 */
export type FlowControls = {
  copy: () => void;
  cut: () => void;
  paste: () => void;
  remove: () => void;
  group: (kind: GroupData["kind"]) => void;
  ungroup: () => void;
  /** Arrange this level and its contents; false when locked or already arranged. */
  arrange: () => boolean;
  comment: (mode?: "out" | "through") => void;
  rotate: (cw?: boolean) => void;
  flip: (axis?: "x" | "y") => void;
  duplicate: () => void;
  can: {
    copy: boolean;
    paste: boolean;
    remove: boolean;
    group: boolean;
    frame: boolean;
    story: boolean;
    ungroup: boolean;
    arrange: boolean;
    comment: boolean;
    rotate: boolean;
    flip: boolean;
    duplicate: boolean;
  };
};

const NO_CONNECTORS: Connectors = {};

/** Blocks by palette group, groups in catalog order. */
export function FlowEditor({ blocks, connectors = NO_CONNECTORS, nodes, edges, setNodes, setEdges, onNodesChange, onEdgesChange, minimap, level: wanted, setLevel, locked, controls }: Props) {
  const states = useContext(RunStates);
  // ---- Levels (#68): a subsystem or story opens on the canvas; the way back is the breadcrumb ----
  const { level, go, path } = useFlowLevels(wanted, setLevel, nodes, edges, setNodes, setEdges);
  // Which wires may join which ports, and the type a new one takes.
  const rules = wireRules(nodes, connectors);
  const { defOf, isValidConnection } = rules;
  const shown = display(nodes, edges, (node, port, side) => portType(defOf(node), port, side) ?? "signal", level);
  const shownIds = new Set(shown.nodes.filter((n) => !n.hidden).map((n) => n.id));
  const { actions, stateOf, canGroup, canFrame, canStory, group } = useFlowGroups(nodes, shownIds, setNodes, setEdges);
  const { lit, follow, unlit } = useLitWires(edges);

  // ---- Adding: quick add at the cursor, and cards dropped from the browser (#50) ----
  const { screenToFlowPosition, deleteElements, fitView } = useReactFlow();

  const store = useStoreApi();
  const latest = useRef({ nodes, edges, level });
  latest.current = { nodes, edges, level };
  // One state update makes the whole recursive arrangement one undo step (#103).
  const arrange = (): boolean => {
    if (locked) return false;
    const current = latest.current;
    const next = arrangeFlowNodes(current.nodes, current.edges, current.level, (nodeId, handleId, dir) => {
      const n = store.getState().nodeLookup.get(nodeId);
      const bounds = n?.internals.handleBounds;
      const list = dir === "source" ? bounds?.source : bounds?.target;
      const h = list?.find((x) => (x.id ?? null) === (handleId ?? null));
      return h ? h.y + h.height / 2 : undefined;
    });
    if (next === current.nodes) return false;
    latest.current = { ...current, nodes: next };
    setNodes(next);
    window.setTimeout(() => fitView({ padding: 0.2, duration: 200, maxZoom: 1.2 }), 30);
    return true;
  };
  // ---- Copy, cut and paste: Ctrl+C / X / V (⌘ on a Mac) on the Flow page, except while typing ----
  const { copied, copySelected, pasteCopied, duplicateSelected } = useFlowClipboard(store, latest, setNodes, setEdges);

  // ---- Drawing and moving wires; while one is dragged (#52), the port it would join is raised ----
  const { wireDrag, handlers } = useWireDrag(rules, setEdges, store, screenToFlowPosition);
  const iconWires = useMemo(() => iconWireCounts(nodes, edges), [nodes, edges]);
  const wrap = useRef<HTMLDivElement>(null);
  const addable = useMemo<Item[]>(() => blocks.filter((b) => !b.linked).map((def) => ({ kind: "block", def })), [blocks]);
  const [quick, setQuick] = useState<{ x: number; y: number } | null>(null);
  const pointer = useRef<{ x: number; y: number } | null>(null); // over the canvas, else null

  const place = (def: BlockDef, screen: { x: number; y: number }) => {
    const at = screenToFlowPosition(screen);
    let id = "";
    setNodes((ns) => {
      const [next, made] = withBlock(ns, def, { x: at.x - BLOCK_SIZE.w / 2, y: at.y - 16 }, level);
      id = made;
      return next;
    });
    return () => id;
  };

  // ---- Several at once: dragging on empty canvas draws a box; a dialog acts on it (#54) ----
  const [busy, setBusy] = useState(false); // dragging blocks or the box: the dialog waits
  const pickedNodes = shown.nodes.filter((n) => n.selected && !n.hidden);
  const pickedBlocks = pickedNodes.filter(isBlock);
  const pickedWires = shown.edges.filter((w) => w.selected);
  const pickedFrames = pickedNodes.filter(isFrame);
  // One block picked inside an open subsystem or story: its ports can be exposed on the group.
  const levelGroup = level ? nodes.find((n): n is GroupNode => n.id === level && isFrame(n)) : undefined;
  const pickedBlock = pickedNodes.length === 1 && isBlock(pickedNodes[0]) ? pickedNodes[0] : undefined;
  const kept = pickedNodes.filter((n) => isBlock(n) && n.data.def.linked).length; // linked blocks aren't deleted
  const many = pickedNodes.length > 0; // a block or group, with or without wires (wires alone have ×)
  const allCommented = pickedBlocks.length > 0 && pickedBlocks.every((b) => b.data.commented);

  const toggleComment = (mode: "out" | "through" = "out") => {
    if (pickedBlocks.length === 0) return;
    const shouldComment = !allCommented || (mode === "through" && pickedBlocks.some((b) => b.data.commentMode !== "through"));
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) =>
      ns.map((n) => {
        if (!ids.has(n.id) || !isBlock(n)) return n;
        return {
          ...n,
          data: {
            ...n.data,
            commented: shouldComment,
            commentMode: shouldComment ? mode : undefined,
          },
        };
      }),
    );
  };

  // The dialog floats above the selection, or below it when the selection is at the top of
  // the canvas (above, it would sit under the panel's head, out of reach).
  const pickedIds = pickedNodes.map((n) => n.id).join(",");
  const roomAbove = useStore((st) => {
    const [, ty, zoom] = st.transform;
    let top = Infinity;
    for (const id of pickedIds ? pickedIds.split(",") : []) {
      const n = st.nodeLookup.get(id);
      if (n) top = Math.min(top, n.internals.positionAbsolute.y * zoom + ty);
    }
    return top > 64;
  });
  const deselect = () => {
    setNodes((ns) => ns.map((n) => (n.selected ? { ...n, selected: false } : n)));
    setEdges((ws) => ws.map((w) => (w.selected ? { ...w, selected: false } : w)));
  };
  const removePicked = () =>
    deleteElements({ nodes: pickedNodes.map((n) => ({ id: n.id })), edges: pickedWires.map((w) => ({ id: w.id })) });

  const updateNodeInternals = useUpdateNodeInternals();

  const rotateSelected = (cw: boolean) => {
    if (pickedBlocks.length === 0) return;
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) => ns.map((n) => (!ids.has(n.id) || !isBlock(n) ? n : rotateBlock(n, cw))));
    pickedBlocks.forEach((b) => updateNodeInternals(b.id));
  };

  const flipSelected = (axis: "x" | "y") => {
    if (pickedBlocks.length === 0) return;
    const ids = new Set(pickedBlocks.map((b) => b.id));
    setNodes((ns) => ns.map((n) => (!ids.has(n.id) || !isBlock(n) ? n : flipBlock(n, axis))));
    pickedBlocks.forEach((b) => updateNodeInternals(b.id));
  };

  useCanvasHotkeys({
    hasSelection: many,
    onInsert: () => {
      if (pointer.current) setQuick({ ...pointer.current });
    },
    onRotateCW: () => rotateSelected(true),
    onRotateCCW: () => rotateSelected(false),
    onFlipH: () => flipSelected("x"),
    onFlipV: () => flipSelected("y"),
    onDuplicate: () => void duplicateSelected(),
    onDelete: removePicked,
    onComment: () => toggleComment("out"),
    onBypass: () => toggleComment("through"),
    onEscape: () => deselect(),
  });

  if (controls) {
    controls.current = {
      copy: () => void copySelected(false),
      cut: () => void copySelected(true),
      paste: () => void pasteCopied(),
      remove: removePicked,
      group,
      ungroup: () => pickedFrames.forEach((f) => actions.ungroup(f.id)),
      arrange,
      comment: (mode: "out" | "through" = "out") => toggleComment(mode),
      rotate: (cw = true) => rotateSelected(cw),
      flip: (axis = "x") => flipSelected(axis),
      duplicate: () => void duplicateSelected(),
      can: {
        copy: copySelection(nodes, edges) !== null,
        get paste() {
          return copied.current !== null;
        },
        remove: !(kept === pickedNodes.length && pickedWires.length === 0),
        group: canGroup,
        frame: canFrame,
        story: canStory,
        ungroup: pickedFrames.length > 0,
        arrange: !locked && canArrangeFlow(nodes, level),
        comment: pickedBlocks.length > 0,
        rotate: pickedBlocks.length > 0,
        flip: pickedBlocks.length > 0,
        duplicate: pickedNodes.length > 0,
      },
    };
  }
  useEffect(() => () => {
    if (controls) controls.current = null;
  }, [controls]);

  // A dragged card: plan what dropping it here would do, show that, and do it on drop.
  const { hint, setHint, onDragOver, onDrop } = useCardDrop(wrap, nodes, edges, level, defOf, setNodes, setEdges, screenToFlowPosition);
  const nodeLookup = useStore((st) => st.nodeLookup);
  const wireRoutes = useMemo(() => routeCanvasWires(nodeLookup, shown.edges), [shown.edges, nodeLookup]);
  const setBends = useMemo(
    () => (locked ? null : (id: string, bends: WirePoint[]) => setEdges((ws) => ws.map((w) => (w.id === id ? { ...w, data: { type: w.data?.type ?? "signal", ...w.data, points: bends } } : w)))),
    [locked, setEdges],
  );

  const box = wrap.current?.getBoundingClientRect();
  const rel = (x: number, y: number) => ({ x: x - (box?.left ?? 0), y: y - (box?.top ?? 0) });
  const commentedOut = useMemo(() => commentedOutIds(nodes), [nodes]);

  return (
    <GroupActions.Provider value={{ ...actions, enter: go, stateOf }}>
    <HotWire.Provider value={hint?.plan.kind === "insert" ? hint.plan.wire.id : null}>
    <Lit.Provider value={lit}>
    <WireRoutes.Provider value={wireRoutes}>
    <SetBends.Provider value={setBends}>
    <ConnectorsContext.Provider value={connectors}>
    <IconWires.Provider value={iconWires}>
    <CommentedOutContext.Provider value={commentedOut}>
    <div className="flow">
      <div
        className="flow-canvas"
        ref={wrap}
        onMouseOver={follow}
        onMouseMove={(e) => (pointer.current = { x: e.clientX, y: e.clientY })}
        onMouseLeave={() => {
          pointer.current = null;
          unlit();
        }}
        onDoubleClick={(e) => {
          // Empty canvas only: not a block, a wire, a port or a control.
          const t = e.target as Element;
          if (!t.closest(".react-flow__node, .react-flow__edge, .react-flow__handle, .react-flow__panel, button, input, select"))
            setQuick({ x: e.clientX, y: e.clientY });
        }}
        onDragOver={onDragOver}
        onDragLeave={(e) => !wrap.current?.contains(e.relatedTarget as globalThis.Node | null) && setHint(null)}
        onDrop={onDrop}
      >
        <WireDragContext.Provider value={wireDrag}>
        <ReactFlow
          nodes={shown.nodes}
          edges={shown.edges}
          nodeTypes={nodeTypes}
          edgeTypes={edgeTypes}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          {...handlers}
          isValidConnection={isValidConnection}
          connectionRadius={SNAP.connect}
          reconnectRadius={SNAP.reconnect}
          deleteKeyCode={["Backspace", "Delete"]}
          multiSelectionKeyCode={["Control", "Meta"]}
          selectionKeyCode={["Control", "Meta"]} // Ctrl-drag on the canvas draws a box…
          selectionMode={SelectionMode.Partial} // …and picks every block it touches; a plain drag pans
          panActivationKeyCode={null} // Space belongs to the backend (Key blocks)
          onNodeDragStart={() => setBusy(true)}
          onNodeDragStop={() => setBusy(false)}
          onSelectionDragStart={() => setBusy(true)}
          onSelectionDragStop={() => setBusy(false)}
          onSelectionStart={() => setBusy(true)}
          onSelectionEnd={() => setBusy(false)}
          zoomOnDoubleClick={false} // double-click is quick add
          fitView
        >
          <Background />
          {level && <LevelCrumbs level={level} path={path} go={go} rename={(id, label) => actions.update(id, { label })} />}
          {minimap && (
            <MiniMap
              pannable
              zoomable
              ariaLabel="Overview of the flow: click or drag to move the view"
              // Words, not colours, tell kinds apart (DESIGN.md): grey blocks, groups as outlines,
              // the signal colour for running, red for failed.
              nodeColor={(n) => (n.type === "frame" && (n.data as GroupData).kind === "visual" ? "transparent" : states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeColor={(n) => (states.failed.has(n.id) ? "var(--danger)" : states.active.has(n.id) ? "var(--signal)" : "var(--line-strong)")}
              nodeStrokeWidth={3}
              nodeBorderRadius={2}
              maskColor="var(--surface-float)"
              maskStrokeColor="var(--line-ink)"
              maskStrokeWidth={1}
            />
          )}
          <SelectionDialog
            pickedNodes={pickedNodes}
            pickedBlocks={pickedBlocks}
            pickedWires={pickedWires}
            pickedFrames={pickedFrames}
            isVisible={many && !busy}
            roomAbove={roomAbove}
            allCommented={allCommented}
            kept={kept}
            canGroup={canGroup}
            canFrame={canFrame}
            canStory={canStory}
            toggleComment={toggleComment}
            removePicked={removePicked}
            group={group}
            ungroup={actions.ungroup}
            update={actions.update}
            levelGroup={levelGroup}
            pickedBlock={pickedBlock}
          />
        </ReactFlow>
        </WireDragContext.Provider>
        {shown.nodes.length === 0 && (
          <p className="canvas-empty">
            <span>
              <span className="kbd">/</span> or double-click to add a block
            </span>
          </p>
        )}
        <CanvasHelp gestures={GESTURES} />
        {hint && <DropHint hint={hint} rel={rel} />}
      </div>
      {quick && (
        <QuickAdd
          items={addable}
          at={quick}
          onClose={() => setQuick(null)}
          onPick={(it) => {
            if (it.kind === "block") place(it.def, quick);
            setQuick(null);
          }}
        />
      )}
    </div>
    </CommentedOutContext.Provider>
    </IconWires.Provider>
    </ConnectorsContext.Provider>
    </SetBends.Provider>
    </WireRoutes.Provider>
    </Lit.Provider>
    </HotWire.Provider>
    </GroupActions.Provider>
  );
}

/** What the canvas does with a pointer and keys, one line each. */
const GESTURES: [string, string][] = [
  ["Add a block", "`/` or double-click the canvas"],
  ["Select", "Click a block · `Ctrl`-drag a box · `Ctrl`-click adds"],
  ["Rotate", "`R` turns clockwise · `Shift+R` counter-clockwise"],
  ["Flip", "`F` mirrors horizontal · `Alt+F` vertical"],
  ["Duplicate", "`Ctrl+D` · `Alt`-drag a block"],
  ["Act on a selection", "The dialog above it: rotate, flip, duplicate, delete, group, comment"],
  ["Comment out · bypass", "`Ctrl+/` · `Ctrl+Shift+Y`"],
  ["Pan · zoom", "Drag the canvas · the wheel"],
  ["Remove a wire", "Drag its end off the port"],
  ["Move a port", "Drag its name to another side · double-click puts it back"],
  ["Open a group", "Double-click it · `Alt+↑` or `Esc` comes back · ▾ collapses"],
  ["Undo · redo", "`Ctrl+Z` · `Ctrl+Shift+Z`"],
];
