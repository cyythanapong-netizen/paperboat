/**
 * The Scope as a Board instrument (#37): the toolkit's side of it. It turns a scope monitor's
 * channels into what the Scope shows (each named by the output wired into it), keeps its
 * settings in the Board layout, and hands a tap to the host to wire.
 */
import { useLayoutEffect, useRef, useState, type RefObject } from "react";
import type { Channel, InstrumentProps } from "@paperboat/host";
import { Scope, type ScopeSettings } from "@paperboat/shared/scope";

/**
 * The replay needle (#284): a line over the plot at the playhead, on every scope at once. The plot
 * is the shared Scope's own element, so it is measured rather than assumed (legend and caption
 * change its box); a resize re-measures.
 */
function Needle({ host, time, from, to }: { host: RefObject<HTMLDivElement | null>; time: number; from: number; to: number }) {
  const [box, setBox] = useState<{ left: number; top: number; width: number; height: number } | null>(null);
  useLayoutEffect(() => {
    const el = host.current;
    const plot = el?.querySelector(".scope-plot");
    if (!el || !plot) return;
    const measure = () => {
      const whole = el.getBoundingClientRect();
      const area = plot.getBoundingClientRect();
      setBox({ left: area.left - whole.left, top: area.top - whole.top, width: area.width, height: area.height });
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    ro.observe(plot);
    return () => ro.disconnect();
  }, [host]);
  if (!box) return null;
  const fraction = to > from ? Math.min(1, Math.max(0, (time - from) / (to - from))) : 0;
  return (
    <div className="replay-needle" style={{ left: box.left + fraction * box.width, top: box.top, height: box.height }} aria-hidden>
      <span>{time.toFixed(2)} s</span>
    </div>
  );
}

export function ScopeInstrument({ name, state, settings, onSettings, size, sources, inputs, taps, onTap }: InstrumentProps) {
  const needleHost = useRef<HTMLDivElement>(null);
  const small = size === "normal" || size === "compact";
  // A scalar input reads as its source ("plant.θ"); a vector's components as theirs, prefixed
  // with the source when there is more than one input ("m.y.x").
  const raw = state.traces.channels(name);
  const many = new Set(raw.map((c) => c.id.split(".")[0])).size > 1;
  const channels: Channel[] = raw.map((c) => {
    const [input, ...rest] = c.id.split(".");
    const src = sources[input];
    if (!rest.length) return { ...c, label: src?.label ?? input, unit: c.unit ?? src?.unit };
    const component = rest.join(".");
    return { ...c, label: many ? `${src?.label ?? input}.${component}` : component, unit: c.unit ?? src?.unit };
  });
  const live = state.run?.status === "running";
  const wired = inputs.filter((i) => sources[i]).map((i) => sources[i].label);
  const scopeSettings = (settings ?? {}) as ScopeSettings;
  // The replay needle marks a time on the time axis; an FFT (or xy) Scope has no time axis.
  const replay = state.replay;
  const showNeedle = replay?.active === true && (scopeSettings.mode ?? "time") === "time";
  return (
    <div className="scope-replay" ref={needleHost}>
      <Scope
        name={name}
        channels={channels}
        version={state.traceVersion}
        live={live}
        settings={scopeSettings}
        onSettings={onSettings}
        small={small}
        placeholder={wired.length ? `${wired.join(" · ")}: waits for a run` : `scope: wire outputs into ${inputs.join(", ") || "its inputs"}, or + tap`}
        taps={taps}
        onTap={onTap}
      />
      {showNeedle && replay && <Needle host={needleHost} time={replay.time} from={replay.from} to={replay.to} />}
    </div>
  );
}
