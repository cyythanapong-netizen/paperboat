import { useMemo } from "react";
import type { InstrumentProps } from "@paperboat/host";

/**
 * A pick-one control: one button per option. It sends the same `input` messages a Key block
 * sees, tagged with this node and the option's index; the Choice block latches the index.
 * It shows only what the engine sends, so a headless or non-running Board stays at the
 * descriptor's initial choice.
 */
export function ChoiceInstrument({ name, latest, state, send, params }: InstrumentProps) {
  const options = useMemo(() => {
    const raw = typeof params?.options === "string" ? params.options : "Negroni, Spritz";
    return raw
      .split(/[,;\n]/)
      .map((o) => o.trim())
      .filter(Boolean);
  }, [params?.options]);
  const live = !!send && state.run?.status === "running";
  const index = typeof latest === "number" ? latest : 0;
  const current = options[Math.max(0, Math.min(index, options.length - 1))] ?? "—";

  const choose = (i: number) => {
    if (!live) return;
    const key = `${name}:${i}`;
    send?.({ type: "input", key, down: true });
    send?.({ type: "input", key, down: false });
  };

  return (
    <figure className={`instrument choice-instrument${live ? "" : " idle"}`}>
      <div className="screen choice-screen">
        <div className="choice-options">
          {options.map((option, i) => (
            <button
              key={option}
              type="button"
              className={`choice-option${i === index ? " on" : ""}`}
              disabled={!live}
              onClick={(e) => {
                e.stopPropagation();
                choose(i);
              }}
              title={`Choose ${option}`}
            >
              {option}
            </button>
          ))}
        </div>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{current}</output>
      </figcaption>
    </figure>
  );
}
