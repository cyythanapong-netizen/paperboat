import type { FlowType } from "./protocol";
import { GlideBrackets } from "./Shell";
import { flowLabel } from "./flowTypes.ts";

/**
 * The open sheet's flow type (#9), in the Flow page's head: a switch between the types the
 * backend offers at the top level (the palette offers the blocks that work in the one picked),
 * or a plain label inside a story, which runs its blocks as a Signal flow.
 */
export function SheetFlow({
  offered,
  flow,
  story,
  chosen,
  disabled,
  onChoose,
}: {
  offered: FlowType[];
  flow: FlowType;
  /** The story the open level is in, if any. */
  story?: string;
  /** False while the type is only taken from the sheet's blocks. */
  chosen: boolean;
  disabled?: boolean;
  onChoose: (flow: FlowType) => void;
}) {
  if (story)
    return (
      <span className="sheet-flow lbl" title="A story runs its blocks as a Signal flow: the palette offers the blocks that work in one">
        {flowLabel(flow)} · story
      </span>
    );
  if (offered.length < 2 && offered.includes(flow))
    return (
      <span className="sheet-flow lbl" title={`This sheet is a ${flowLabel(flow)}: the only flow type this backend offers`}>
        {flowLabel(flow)}
      </span>
    );
  if (!offered.includes(flow))
    return (
      <span className="sheet-flow lbl" title="A block on this sheet makes it this flow type; the backend doesn't offer it as a choice">
        {flowLabel(flow)} · from the blocks
      </span>
    );
  const types = offered;
  return (
    <div className="switch sheet-flow" role="radiogroup" aria-label="Sheet flow type">
      {types.map((f) => (
        <button
          key={f}
          role="radio"
          aria-checked={f === flow}
          className={`opt${f === flow ? " on" : ""}`}
          disabled={disabled}
          title={
            f === flow
              ? `This sheet is a ${flowLabel(f)}${chosen ? "" : " (from its blocks)"}: the palette offers the blocks that work in one`
              : `Make this sheet a ${flowLabel(f)}: the palette then offers the blocks that work in one`
          }
          onClick={() => onChoose(f)}
        >
          {flowLabel(f)}
        </button>
      ))}
      <GlideBrackets />
    </div>
  );
}
