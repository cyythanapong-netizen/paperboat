import { useEffect, useRef } from "react";
import type { EngineState } from "./engine";
import type { ClientMsg, ServerMsg, Station } from "./protocol";

type Props = {
  /** The backend's WebSocket URL: the page is served over HTTP on the same host and port. */
  backend: string;
  /** The page's path on that port (hello.station.page). */
  page: string;
  state: EngineState;
  subscribe: (listen: (m: ServerMsg) => void) => () => void;
  send: (m: ClientMsg) => void;
  onStation: (station: Station) => void;
};

/** http(s)://host:port/page for a ws(s)://host:port backend. */
export function pageUrl(backend: string, page: string): string {
  const url = new URL(backend);
  url.protocol = url.protocol === "wss:" ? "https:" : "http:";
  url.pathname = page;
  url.search = "";
  return url.toString();
}

/**
 * A web station: the backend's own page, shown in place of the built-in scene (GUIDELINES
 * §6.4). The page is sandboxed (scripts only, no same-origin: it can't reach the app) and
 * talks to the app through the bridge it loads from the backend: it hears every protocol
 * message, and may send back only what the Station could (`station.set`, `input`).
 */
export function WebStation({ backend, page, state, subscribe, send, onStation }: Props) {
  const frame = useRef<HTMLIFrameElement>(null);
  const latest = useRef(state);
  latest.current = state;

  useEffect(() => {
    const post = (message: unknown) => frame.current?.contentWindow?.postMessage({ paperboat: 0, message }, "*");
    // What a page that just loaded missed: the handshake and the current scene and station.
    const replay = () => {
      const s = latest.current;
      post({ type: "hello", protocol: "paperboat/0", engine: s.engine, station: s.stationView });
      post({ type: "catalog", blocks: s.blocks, objects: s.objects });
      post({ type: "scene", entities: Object.values(s.entities), view: s.view });
      if (s.station) post({ type: "station.load", station: s.station });
    };
    const onMessage = (e: MessageEvent) => {
      if (e.source !== frame.current?.contentWindow || e.data?.paperboat !== 0) return;
      const m = e.data.message;
      if (m?.type === "station.ready") replay();
      else if (m?.type === "input" && typeof m.key === "string") send({ type: "input", key: m.key, down: !!m.down });
      else if (m?.type === "station.set" && Array.isArray(m.station?.objects)) onStation(m.station);
    };
    window.addEventListener("message", onMessage);
    const unsubscribe = subscribe(post);
    return () => {
      window.removeEventListener("message", onMessage);
      unsubscribe();
    };
  }, [subscribe, send, onStation]);

  return (
    <iframe
      ref={frame}
      className="web-station"
      title="Station (the backend's page)"
      sandbox="allow-scripts"
      src={pageUrl(backend, page)}
    />
  );
}
