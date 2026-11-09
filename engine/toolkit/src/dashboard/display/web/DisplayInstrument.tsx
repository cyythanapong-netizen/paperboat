import type { InstrumentProps } from "@paperboat/host";
import { readout } from "./readout";

export function DisplayInstrument({ name, latest, vector, idle }: InstrumentProps) {
  const { text, count } = readout(latest, vector);
  return (
    <figure className={`instrument display${idle ? " idle" : ""}`}>
      <div className="screen">
        <output>{text}</output>
      </div>
      <figcaption>
        <span>{name}</span>
        <span>{count === undefined ? "display" : `${count} ${count === 1 ? "value" : "values"}`}</span>
      </figcaption>
    </figure>
  );
}
