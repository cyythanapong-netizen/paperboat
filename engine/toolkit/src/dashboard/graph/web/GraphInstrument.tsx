import type { Channel, InstrumentProps } from "@paperboat/host";

/** A Graph: the glance. One trace, or one per component of a vector (a clock run), and the latest values. */
function Plot({ name, channels, idle }: { name: string; channels: readonly Channel[]; idle: boolean }) {
  const W = 300;
  const H = 100;
  // x runs from the least x of the samples: time, a list's index, or a point's x (#24).
  let tMin = Infinity, tMax = -Infinity, vMin = 0, vMax = 0;
  for (const c of channels)
    for (let i = 0; i < c.buf.n; i++) {
      tMin = Math.min(tMin, c.buf.t[i]);
      tMax = Math.max(tMax, c.buf.t[i]);
      vMin = Math.min(vMin, c.buf.v[i]);
      vMax = Math.max(vMax, c.buf.v[i]);
    }
  if (!Number.isFinite(tMin)) tMin = 0;
  if (!Number.isFinite(tMax)) tMax = 0;
  const span = vMax - vMin || 1;
  const width = tMax - tMin || 1;
  const points = (c: Channel) => {
    let out = "";
    for (let i = 0; i < c.buf.n; i++) out += `${((c.buf.t[i] - tMin) / width) * W},${H - ((c.buf.v[i] - vMin) / span) * H} `;
    return out;
  };
  const lasts = channels.map((c) => c.buf.last);
  const readout =
    lasts[0] === undefined
      ? "–"
      : channels.length === 1
        ? lasts[0].toFixed(3)
        : channels.map((c, i) => `${c.label} ${lasts[i]?.toFixed(2)}`).join("  ");
  return (
    <figure className={`instrument graph${idle ? " idle" : ""}`}>
      <div className="screen">
        <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
          <line className="zero" x1={0} x2={W} y1={H - (-vMin / span) * H} y2={H - (-vMin / span) * H} />
          {channels.map((c, i) => (
            <polyline key={c.id} className={`trace trace-${i % 4}`} points={points(c)} fill="none" vectorEffect="non-scaling-stroke">
              {channels.length > 1 && <title>{c.label}</title>}
            </polyline>
          ))}
        </svg>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{readout}</output>
      </figcaption>
    </figure>
  );
}

/** The Graph instrument: one trace per channel of the monitor, over time. */
export function GraphInstrument({ name, state, idle }: InstrumentProps) {
  return <Plot name={name} channels={state.traces.channels(name)} idle={idle} />;
}
