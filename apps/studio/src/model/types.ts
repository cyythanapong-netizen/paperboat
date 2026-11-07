// The seam between Lane A and Lane B in apps/studio (contracts §8). Frozen: change only through
// the orchestrator. Lane A implements StudioApi and provides it; Lane B only consumes it.
import { createContext, useContext } from "react";
import type { Connectors, IconDef, IconPort, ParamDef } from "@paperboat/shared";
import type { OmcClient, OmcStatus } from "../omc/omc.ts";

/** Diagram-layer placement, on the 20px canvas grid. */
export type Placement = { x: number; y: number; rotation: 0 | 90 | 180 | 270; mirror?: boolean };

export type ComponentInst = {
  name: string; // "R1"
  class: string; // "Modelica.Electrical.Analog.Basic.Resistor"
  params: Record<string, string>; // modifiers as Modelica text: { R: "10" }
  placement: Placement;
  icon?: IconDef; // from the Wave 2 importer
  ports: IconPort[];
};

/** An undirected physical connection or a directed causal one: "R1.n" to "L1.p". */
export type Connection = { a: string; b: string; points?: [number, number][] };

/** A top-level causal entry point (+ Input on a port, + Probe on a variable). */
export type EntryPoint = { name: string; kind: "input" | "output"; target: string; unit?: string };

export type StudioModel = {
  name: string; // qualified class name being edited
  file?: string; // the .mo on disk
  components: ComponentInst[];
  connections: Connection[];
  params: ParamDef[]; // top-level parameters: the generated object's Station props
  entries: EntryPoint[];
  icon?: IconDef; // the model's own look (Look tab), the generated linked block's view.icon
  connectors: Connectors;
};

export type ExportProgress = { stage: "check" | "flatten" | "generate" | "build"; fraction: number };
export type ExportResult = { out: string; path: "ode" | "fmu"; reason: string };

export interface StudioApi {
  model: StudioModel;
  selection: string[]; // component names or "R1.n" port paths
  status: OmcStatus;
  omc: OmcClient; // raw squero/0 calls (checkModel for the balance count)
  select(names: string[]): void;
  /** "R1.R" sets a component modifier; "J" (no dot) sets a top-level parameter's default. */
  setParam(path: string, value: string): Promise<void>;
  addComponent(cls: string, name: string, at: Placement): Promise<void>;
  /** null when connected, else the words why not (contracts §4 rules). */
  connect(a: string, b: string): Promise<string | null>;
  /** + Input on a causal input port, + Probe on a variable or output port. */
  addEntry(kind: "input" | "output", target: string): Promise<void>;
  removeEntry(name: string): Promise<void>;
  /** Writes the model's Icon annotation (Look tab). */
  setIcon(icon: IconDef): Promise<void>;
  exportBackend(out: string, onProgress: (p: ExportProgress) => void): Promise<ExportResult>;
}

export const StudioContext = createContext<StudioApi | null>(null);

export function useStudio(): StudioApi {
  const api = useContext(StudioContext);
  if (!api) throw new Error("useStudio outside StudioContext");
  return api;
}
