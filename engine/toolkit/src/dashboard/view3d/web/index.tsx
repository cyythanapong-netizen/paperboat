// The 3D view block's instrument: the Station's scene, on the Board. Registered by the app.
import type { ToolkitWebHost } from "@paperboat/host";
import { View3DInstrument } from "./View3DInstrument";
import "./view3d.css";

export function register(host: ToolkitWebHost): void {
  host.registerInstrument("view3d", View3DInstrument, { size: "large", order: 1 });
}
