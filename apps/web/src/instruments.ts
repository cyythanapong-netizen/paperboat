import type { ComponentType } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, InstrumentSettings, ParamValue } from "./protocol";
import type { Size } from "./dashboardLayout";

/** An output a monitor can be wired to ("+ tap"): an id the host understands, and what to show. */
export type TapPort = { id: string; label: string; unit?: string };
/** What is wired into each of a monitor's inputs: the output's name ("plant.θ") and unit. */
export type Sources = Record<string, { label: string; unit?: string }>;

/**
 * What every Dashboard instrument gets: its monitor, the latest value and the live engine state
 * (`state.traces` holds the history of every monitor's signals), and the host's services for it:
 * its cell's size, what feeds its inputs, its own saved settings, and wiring a tap in.
 */
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
  /** The size of its cell. */
  size: Size;
  /** The ids of the monitor's inputs, and what is wired into each. */
  inputs: string[];
  sources: Sources;
  /** The instrument's own settings, kept in the Board layout (saved with the project); `onSettings(undefined)` drops them. */
  settings?: InstrumentSettings;
  onSettings?: (next: InstrumentSettings | undefined) => void;
  /** Every block output that carries numbers, and wiring one into this monitor's first free input (growing it if its `inputs` param allows). */
  taps: readonly TapPort[];
  onTap?: (id: string) => void;
}

export type InstrumentRenderer = ComponentType<InstrumentProps>;

/** What the toolkit that provides an instrument says about its place on the Board. */
export interface InstrumentMeta {
  /** The size it opens at: a readout is a card, a plot is read at a glance, a picture is studied. Default normal. */
  size?: Size;
  /** Where it goes in the automatic order, lowest first (readouts 0, pictures 1, plots 2). Default 2. */
  order?: number;
}

const renderers = new Map<string, { renderer: InstrumentRenderer; meta: InstrumentMeta }>();

/** Render monitors whose block says `instrument = kind` with this component; a later call replaces it. */
export function registerInstrument(kind: string, renderer: InstrumentRenderer, meta: InstrumentMeta = {}): void {
  renderers.set(kind, { renderer, meta });
}

/** What the instrument for `kind` said about its size and order (empty when nothing registered it). */
export function instrumentMeta(kind: string): InstrumentMeta {
  return renderers.get(kind)?.meta ?? {};
}

/** The renderer for an instrument kind, or undefined when nothing registered it. */
export function instrumentFor(kind: string): InstrumentRenderer | undefined {
  return renderers.get(kind)?.renderer;
}

/** What the web app hands a toolkit's `web/index.ts(x)`: the registries it may extend. */
export interface ToolkitWebHost {
  registerInstrument: typeof registerInstrument;
}

/**
 * Toolkit UI (pb#172): a block's folder may hold `web/index.ts(x)` exporting `register(host)` (a
 * toolkit that has one UI for the whole package may keep it in `<package>/web/`). They are bundled
 * at build time, so a toolkit installed into engine/toolkit/<name> shows up on the next web build
 * (or at once under the dev server); the app names no toolkit and no block.
 */
const toolkitWeb = import.meta.glob<{ register?: (host: ToolkitWebHost) => void }>(
  [
    "../../../engine/toolkit/src/*/*/web/index.{ts,tsx}", // a built-in block's
    "../../../engine/toolkit/*/*/web/index.{ts,tsx}", // an installed toolkit's block
    "../../../engine/toolkit/*/web/index.{ts,tsx}", // an installed toolkit's, for the whole package
    "../../../engine/toolkit/src/*/web/index.{ts,tsx}", // a built-in package's, for the whole package
  ],
  { eager: true },
);
for (const [path, mod] of Object.entries(toolkitWeb)) {
  if (typeof mod.register === "function") mod.register({ registerInstrument });
  else console.warn(`${path}: a toolkit's web/index must export register(host)`);
}
