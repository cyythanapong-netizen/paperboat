import type { InstrumentProps } from "@paperboat/host";
import { lampView } from "./lamp";

/**
 * A Lamp: lit while its block's condition holds (or, latched, from the first time it did). The
 * engine decides; this shows the latest state as a disc in the signal colour (or red) and says
 * it in words beside it, so it never relies on colour alone. The caption holds the condition.
 */
export function LampInstrument({ name, latest, params }: InstrumentProps) {
  const v = lampView(latest, params);
  const classes = ["instrument", "lamp-instrument", v.idle ? "idle" : "", v.on ? "on" : "", `tone-${v.tone}`].filter(Boolean).join(" ");
  return (
    <figure className={classes}>
      <div className="screen lamp-screen">
        <span className="lamp-disc" aria-hidden="true" />
        <span className="lamp-text">
          {v.label && <span className="lamp-label">{v.label}</span>}
          <output className="lamp-word" aria-label={`${name}: ${v.idle ? "no reading yet" : v.word}`}>
            {v.word}
          </output>
        </span>
      </div>
      <figcaption>
        <span>{name}</span>
        <output title={v.latch ? "Latches: stays on until the run ends" : undefined}>
          {v.condition}
          {v.latch ? " · latch" : ""}
        </output>
      </figcaption>
    </figure>
  );
}
