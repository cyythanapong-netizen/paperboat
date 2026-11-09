// The scope block's instrument. The app bundles this file and calls register(host); it names none of it.
import type { ToolkitWebHost } from "@paperboat/host";
import { ScopeInstrument } from "./ScopeInstrument";

export function register(host: ToolkitWebHost): void {
  host.registerInstrument("scope", ScopeInstrument, { size: "large" });
}
