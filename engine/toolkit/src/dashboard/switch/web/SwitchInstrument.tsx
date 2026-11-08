import { useRef } from "react";
import type { InstrumentProps } from "@paperboat/host";

/**
 * A Switch: a control the user flips during a run. It shows only what the engine sends
 * (no local guess), its `initial` state before the first sample, and is inert while no
 * run is going or the Dashboard can't send. Toggle mode has a click pill; momentary mode
 * a hold button, so the two never share a press.
 */
export function SwitchInstrument({ name, latest, state, send, params }: InstrumentProps) {
  const momentary = params?.mode === "momentary";
  const initialOn = params?.initial === undefined ? true : !!params.initial;
  const live = !!send && state.run?.status === "running";
  const on = live && typeof latest === "number" ? latest > 0.5 : initialOn;
  const held = useRef(false);
  const input = (down: boolean) => send?.({ type: "input", key: name, down });

  // The engine latches a press until its next pass, so a click is a press and a release.
  const click = (e: React.MouseEvent) => {
    e.stopPropagation();
    if (!live) return;
    input(true);
    input(false);
  };
  const press = (e: React.PointerEvent) => {
    e.stopPropagation();
    if (!live) return;
    e.currentTarget.setPointerCapture(e.pointerId); // the release arrives even off the button
    held.current = true;
    input(true);
  };
  const release = (e: React.PointerEvent) => {
    e.stopPropagation();
    if (!held.current) return;
    held.current = false;
    input(false);
  };

  return (
    <figure className={`instrument switch-instrument${live ? "" : " idle"}`}>
      <div className="screen switch-screen">
        <div className="switch-controls">
          {momentary ? (
            <button
              type="button"
              className={`switch-momentary-btn${on ? " on" : ""}`}
              disabled={!live}
              onPointerDown={press}
              onPointerUp={release}
              onPointerCancel={release}
              title="Hold to switch on"
            >
              HOLD
            </button>
          ) : (
            <button
              type="button"
              className={`switch-pill${on ? " on" : ""}`}
              disabled={!live}
              onClick={click}
              title={`Click to switch ${on ? "off" : "on"}`}
            >
              <span className="switch-text">{on ? "ON" : "OFF"}</span>
              <span className="switch-knob" />
            </button>
          )}
        </div>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{on ? "on (1)" : "off (0)"}</output>
      </figcaption>
    </figure>
  );
}
