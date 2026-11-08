import type { InstrumentProps } from "@paperboat/host";

export function DisplayInstrument({ name, latest, vector, idle }: InstrumentProps) {
  return (
    <figure className={`instrument display${idle ? " idle" : ""}`}>
      <div className="screen">
        <output>
          {typeof latest === "number"
            ? +latest.toFixed(4)
            : Array.isArray(latest)
              ? `[${vector ?? latest.map((v) => +v.toFixed(4)).join(", ")}]`
              : String(latest ?? "–")}
        </output>
      </div>
      <figcaption>
        <span>{name}</span>
        <span>display</span>
      </figcaption>
    </figure>
  );
}
