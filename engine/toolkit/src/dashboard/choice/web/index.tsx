// The choice block's instrument. The app bundles this file and calls register(host).
import type { ToolkitWebHost } from "@paperboat/host";
import { ChoiceInstrument } from "./ChoiceInstrument";
import "./choice.css";

export function register(host: ToolkitWebHost): void {
  host.registerInstrument("choice", ChoiceInstrument, { size: "normal" });
}
