import { registerInstrument, type InstrumentProps } from "./instruments";

/** An image monitor (opencv/view): the latest frame as a data URL. */
export function ImageInstrument({ name, latest, idle }: InstrumentProps) {
  return (
    <figure className={`instrument image${idle ? " idle" : ""}`}>
      <div className="screen">
        {typeof latest === "string" && latest ? <img src={latest} alt={name} draggable={false} /> : <output>{idle ? "–" : "no image"}</output>}
      </div>
      <figcaption>
        <span>{name}</span>
        <span>image</span>
      </figcaption>
    </figure>
  );
}

registerInstrument("image", ImageInstrument);
