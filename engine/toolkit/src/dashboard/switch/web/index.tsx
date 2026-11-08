// The switch block's instrument. The app bundles this file and calls register(host); it names none of it.
import type { ToolkitWebHost } from "@paperboat/host";
import { SwitchInstrument } from "./SwitchInstrument";
import "./switch.css";

export function register(host: ToolkitWebHost): void {
  host.registerInstrument("switch", SwitchInstrument, { size: "compact" });
}
