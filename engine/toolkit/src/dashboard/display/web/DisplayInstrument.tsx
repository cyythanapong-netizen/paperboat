import type { InstrumentProps } from "@paperboat/host";
import { readout } from "./readout";

export function DisplayInstrument({ name, latest, vector, idle }: InstrumentProps) {
  const { dims, type, values } = readout(latest, vector);
  return (
    <figure className={`instrument display${idle ? " idle" : ""}`}>
      <div className="screen">
        {dims && <span className="dims">{dims}</span>}
        <output>{type ? `${type}: ${values}` : values}</output>
      </div>
      <figcaption>
        <span>{name}</span>
        <span>display</span>
      </figcaption>
    </figure>
  );
}
