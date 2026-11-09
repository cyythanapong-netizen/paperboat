// The scope block's instrument. The app bundles this file and calls register(host); it names none of it.
import type { ToolkitWebHost } from "@paperboat/host";
import { scopeWidget } from "@paperboat/shared/scope";
import { ScopeInstrument } from "./ScopeInstrument";

export function register(host: ToolkitWebHost): void {
  // The same id the shared Scope marks itself with: the adapter and the widget cannot drift.
  host.registerInstrument(scopeWidget.id, ScopeInstrument, { size: "large" });
}
