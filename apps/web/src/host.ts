// What a toolkit's web code (engine/toolkit/<package>/web/) may import from the app: types only,
// as "@paperboat/host". The app hands the runtime side over in register(host), so a toolkit
// never imports app code and can be added or removed without touching it.
export type { EngineState } from "./engine";
export type { ClientMsg, InstrumentSettings, ParamValue } from "./protocol";
export type { Channel, TraceBuffer, TraceStore } from "./traces";
export type { InstrumentMeta, InstrumentProps, InstrumentRenderer, Sources, TapPort, ToolkitWebHost } from "./instruments";
