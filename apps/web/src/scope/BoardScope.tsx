/**
 * The Board's adapter for a Scope (#37): the app's side of it. It turns a scope monitor's
 * channels into what the Scope shows (each named by the output wired into it), keeps its
 * settings in the Board layout, and turns a tap into a graph edit.
 */
import type { EngineState } from "../engine";
import type { ScopeSettings } from "../protocol";
import { Scope } from "./Scope";
import type { TapPort } from "./Scope";
import type { Channel } from "./trace";

/** What feeds each input of a scope: the output's name ("plant.θ") and unit. */
export type Sources = Record<string, { label: string; unit?: string }>;

type Props = {
  name: string;
  state: EngineState;
  settings?: ScopeSettings;
  onSettings?: (next: ScopeSettings) => void;
  small?: boolean;
  sources?: Sources;
  /** Its inputs, for the placeholder before a run. */
  inputs?: string[];
  taps?: readonly TapPort[];
  onTap?: (id: string) => void;
};

export function BoardScope({ name, state, settings, onSettings, small, sources = {}, inputs = [], taps, onTap }: Props) {
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
  return (
    <Scope
      name={name}
      channels={channels}
      version={state.traceVersion}
      live={live}
      settings={settings ?? {}}
      onSettings={onSettings}
      small={small}
      placeholder={wired.length ? `${wired.join(" · ")}: waits for a run` : `scope: wire outputs into ${inputs.join(", ") || "its inputs"}, or + tap`}
      taps={taps}
      onTap={onTap}
    />
  );
}
