import type { ComponentType } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, ParamValue } from "./protocol";

/** What every Dashboard instrument gets: its monitor, the latest value and the live engine state. */
export interface InstrumentProps {
  name: string;
  /** The block's `instrument`, e.g. "graph", "display". */
  kind: string;
  latest: unknown;
  /** Nothing received yet: draw a greyed placeholder of the same frame (#58). */
  idle: boolean;
  /** A vector value as "id: value unit" text, when latest is an array. */
  vector?: string;
  state: EngineState;
  send?: (m: ClientMsg) => void;
  params?: Record<string, ParamValue>;
}

export type InstrumentRenderer = ComponentType<InstrumentProps>;

const renderers = new Map<string, InstrumentRenderer>();

/** Render monitors whose block says `instrument = kind` with this component; a later call replaces it. */
export function registerInstrument(kind: string, renderer: InstrumentRenderer): void {
  renderers.set(kind, renderer);
}

/** The renderer for an instrument kind, or undefined when nothing registered it. */
export function instrumentFor(kind: string): InstrumentRenderer | undefined {
  return renderers.get(kind);
}
