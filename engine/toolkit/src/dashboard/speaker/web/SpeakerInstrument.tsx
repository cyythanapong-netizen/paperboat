import { useEffect, useRef, useState } from "react";
import type { InstrumentProps } from "@paperboat/host";

/**
 * Plays a PCM vector through Web Audio. The backend sends one control step's samples per
 * signal message; the instrument queues them back to back. Browsers start an AudioContext
 * suspended, so the tile shows a "sound on" control until the user clicks it.
 */
export function SpeakerInstrument({ latest, state, params }: InstrumentProps) {
  const context = useRef<AudioContext | null>(null);
  const nextTime = useRef(0);
  const [enabled, setEnabled] = useState(false);
  const rate = typeof params?.rate === "number" && params.rate > 0 ? params.rate : 44100;
  const volume = typeof params?.volume === "number" ? params.volume : 0.8;
  const live = state.run?.status === "running";

  useEffect(() => {
    return () => {
      void context.current?.close();
      context.current = null;
    };
  }, []);

  const enable = async () => {
    if (!context.current) context.current = new AudioContext({ sampleRate: rate });
    await context.current.resume();
    setEnabled(context.current.state === "running");
  };

  useEffect(() => {
    const ctx = context.current;
    if (!ctx || ctx.state !== "running" || !live) return;
    if (!Array.isArray(latest) || latest.length === 0) return;
    const buffer = ctx.createBuffer(1, latest.length, rate);
    const channel = buffer.getChannelData(0);
    for (let i = 0; i < latest.length; i++) {
      const value = Number(latest[i]);
      channel[i] = Number.isFinite(value) ? Math.max(-1, Math.min(1, value * volume)) : 0;
    }
    const source = ctx.createBufferSource();
    source.buffer = buffer;
    source.connect(ctx.destination);
    const now = ctx.currentTime;
    if (nextTime.current < now || nextTime.current > now + 0.5) nextTime.current = now + 0.03;
    source.start(nextTime.current);
    nextTime.current += buffer.duration;
  }, [latest, live, rate, volume]);

  return (
    <figure className={`instrument speaker-instrument${live ? "" : " idle"}`}>
      <div className="screen speaker-screen">
        <button type="button" className={`speaker-enable${enabled ? " on" : ""}`} onClick={enable}>
          {enabled ? "SOUND ON" : "CLICK FOR SOUND"}
        </button>
        <div className="speaker-bars" aria-hidden="true">
          {Array.from({ length: 18 }, (_, i) => (
            <span key={i} style={{ opacity: enabled && live ? 0.35 + ((i * 7) % 10) / 10 : 0.15 }} />
          ))}
        </div>
      </div>
      <figcaption>
        <span>Speaker</span>
        <output>{Array.isArray(latest) ? `${latest.length} samples` : "waiting"}</output>
      </figcaption>
    </figure>
  );
}
