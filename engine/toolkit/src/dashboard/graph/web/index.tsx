// The graph block's instrument. The app bundles this file and calls register(host); it names none of it.
import type { ToolkitWebHost } from "@paperboat/host";
import { GraphInstrument } from "./GraphInstrument";
import "./graph.css";

export function register(host: ToolkitWebHost): void {
  host.registerInstrument("graph", GraphInstrument);
}
