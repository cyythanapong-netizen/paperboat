import { useReactFlow, useUpdateNodeInternals, type NodeProps } from "@xyflow/react";
import { useContext, useEffect, useState } from "react";
import { CompactBlock } from "./Compact";
import { signOf } from "./ports";
import { iconValid, rotateIcon } from "@paperboat/shared";
import { usePortLayout } from "./portLayout";
import { PageIcon } from "./Shell";
import { ParamRow } from "./ParamFields";
import { ConnectorsContext, IconWires, RunStates } from "./flowContexts";
import { hasStepPorts, inForm, type BlockNode, type Wire } from "./flowModel";
import { runWord } from "./engine";

/** Generic node: everything it shows comes from the backend's BlockDef. */
export function Block({ id, data, selected }: NodeProps<BlockNode>) {
  const { updateNodeData } = useReactFlow<BlockNode, Wire>();
  const states = useContext(RunStates);
  const active = states.active.has(id);
  const progress = states.progress[id];
  const { params, missing, commented, commentMode, comment } = data;
  const def = inForm(data.def, params);
  const failed = states.failed.has(id);
  const word = runWord(states, id);
  const connectors = useContext(ConnectorsContext);
  const iconWires = useContext(IconWires);
  const [noteOpen, setNoteOpen] = useState(false);
  // Ports that follow a param come and go: have the canvas measure the handles again.
  const updateInternals = useUpdateNodeInternals();
  const portKey = [...def.inputs, ...def.outputs].map((p) => p.id).join(",");
  useEffect(() => updateInternals(id), [portKey, id, updateInternals, data.rotation, data.mirror, data.ports]);
  // A simple block draws as a small plate (#70) until double-clicked open; fold puts it back.
  const [expanded, setExpanded] = useState(false);
  const ports = usePortLayout({
    id,
    def,
    sides: data.ports,
    save: (ports) => updateNodeData(id, { ports }),
    disabled: missing,
    sign: (p, dir) => (dir === "in" ? signOf(def, params, def.inputs.indexOf(p)) : ""),
  });
  const placedIcon = (data.rotation || data.mirror) && def.view?.icon ? rotateIcon(def.view.icon, { rotation: data.rotation, mirror: data.mirror }) : def.view?.icon;
  const viewDef = placedIcon ? { ...def, view: { ...def.view, icon: placedIcon } } : def;
  if ((def.view?.compact || iconValid(def.view?.icon)) && !expanded && !missing && !hasStepPorts(def)) {
    return (
      <CompactBlock
        id={id}
        def={viewDef}
        sides={data.ports}
        params={params}
        selected={!!selected}
        commented={commented}
        word={word}
        progress={progress}
        onExpand={() => setExpanded(true)}
        connectors={connectors}
        wires={iconWires.count.get(id)}
        popover={def.params.map((p) => (
          <ParamRow key={p.id} def={p} value={params[p.id]} set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })} />
        ))}
      />
    );
  }
  const step = hasStepPorts(def);
  const mode = commentMode ?? "out";
  const classes = [
    "block",
    `kind-${def.kind}`,
    step && "step",
    selected && "selected",
    active && "active",
    def.linked && "linked",
    missing && "missing",
    failed && "failed",
    commented && "commented",
    commented && mode === "through" && "comment-through",
  ];
  // The head's tag says what is happening to the block, else what it is: words, not colours.
  const tag = commented
    ? mode === "through"
      ? "// bypass"
      : "// commented"
    : missing
      ? "missing"
      : word
        ? word
        : def.linked
          ? "linked"
          : step
            ? "step"
            : def.kind === "function"
              ? "fn"
              : def.kind;
  return (
    <div
      className={classes.filter(Boolean).join(" ")}
      title={missing ? `This backend has no "${def.type}" block: its params and wires are kept. Replace or delete it.` : undefined}
    >
      {ports.top}
      <div className="block-title">
        <span className="block-name" title={def.linked ? `Linked to station object ${id}: set its properties, or remove it, on the Station` : undefined}>
          {missing ? def.type : def.label}
          {def.linked && <span className="linked-tag"> <PageIcon page="station" className="link-mark" />{id}</span>}
        </span>
        <button
          type="button"
          className={`block-note-btn nodrag ${comment ? "has-note" : ""}`}
          title={comment ? `Note: ${comment}` : "Add note"}
          onClick={() => setNoteOpen((o) => !o)}
        >
          {comment ? "note*" : "note"}
        </button>
        <span className={`block-tag tag-${tag}`}>{tag}</span>
        {def.view?.compact && (
          <button className="fold nodrag" title="Fold back to a plate" onClick={() => setExpanded(false)}>
            fold
          </button>
        )}
      </div>
      {noteOpen && (
        <div className="block-note-well nodrag nowheel nopan">
          <textarea
            className="block-note-input"
            placeholder="Notes or formulas..."
            value={comment ?? ""}
            onChange={(e) => updateNodeData(id, { comment: e.target.value })}
          />
        </div>
      )}
      {active && progress !== undefined && progress < 1 && (
        <div className="block-progress" title={`${Math.round(progress * 100)} %`}>
          <div style={{ width: `${progress * 100}%` }} />
        </div>
      )}
      {ports.rows}
      {def.params.map((p) => (
        <ParamRow
          key={p.id}
          def={p}
          value={params[p.id]}
          disabled={missing}
          set={(v) => updateNodeData(id, { params: { ...params, [p.id]: v } })}
        />
      ))}
      {ports.bottom}
    </div>
  );
}
