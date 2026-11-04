import type { EngineState, Series } from "./engine";

function Plot({ name, series }: { name: string; series: Series }) {
  const W = 300;
  const H = 100;
  const tMax = Math.max(...series.t, 1e-9);
  const vMin = Math.min(...series.v, 0);
  const vMax = Math.max(...series.v, 0);
  const span = vMax - vMin || 1;
  const points = series.t.map((t, i) => `${(t / tMax) * W},${H - ((series.v[i] - vMin) / span) * H}`).join(" ");
  const last = series.v[series.v.length - 1] ?? 0;
  return (
    <figure className="instrument graph">
      <div className="screen">
        <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
          <line className="zero" x1={0} x2={W} y1={H - (-vMin / span) * H} y2={H - (-vMin / span) * H} />
          <polyline className="trace" points={points} fill="none" vectorEffect="non-scaling-stroke" />
        </svg>
      </div>
      <figcaption>
        <span>{name}</span>
        <output>{last.toFixed(3)}</output>
      </figcaption>
    </figure>
  );
}

/** One instrument: how a monitor shows on the Dashboard is up to its block's `instrument`. */
function Instrument({ name, kind, state }: { name: string; kind: string; state: EngineState }) {
  const latest = state.latest[name];
  if (kind === "image")
    return (
      <figure className="instrument image">
        <div className="screen">{typeof latest === "string" && <img src={latest} alt={name} />}</div>
        <figcaption>
          <span>{name}</span>
          <span>image</span>
        </figcaption>
      </figure>
    );
  if (kind === "display")
    return (
      <figure className="instrument display">
        <div className="screen">
          <output>{typeof latest === "number" ? +latest.toFixed(4) : (latest ?? "–")}</output>
        </div>
        <figcaption>
          <span>{name}</span>
          <span>display</span>
        </figcaption>
      </figure>
    );
  const series = state.signals[name];
  return series ? <Plot name={name} series={series} /> : null;
}

/** A monitor on the Flow canvas: what the Dashboard needs to place its instrument. */
export type Monitor = { kind: string; x: number; y: number };

/**
 * The Dashboard lays itself out, with no layout to edit: readouts (displays) in a strip of
 * small cards on top, then pictures, then plots as equal tiles in a grid that fits as many
 * columns as the width allows. Within a kind, instruments follow the Flow canvas, left to
 * right, then top to bottom: a pipeline's stages read in order, and moving a monitor
 * block moves its instrument.
 */
export function Dashboard({ state, monitors }: { state: EngineState; monitors: Record<string, Monitor> }) {
  const at = (n: string) => monitors[n] ?? { kind: "graph", x: Infinity, y: Infinity };
  const names = Object.keys(state.latest).sort((a, b) => at(a).x - at(b).x || at(a).y - at(b).y);
  const kind = (n: string) => at(n).kind;
  const readouts = names.filter((n) => kind(n) === "display");
  const tiles = [...names.filter((n) => kind(n) === "image"), ...names.filter((n) => !["display", "image"].includes(kind(n)))];
  return (
    <div className="dashboard">
      {readouts.length > 0 && (
        <div className="readouts">
          {readouts.map((n) => (
            <Instrument key={n} name={n} kind="display" state={state} />
          ))}
        </div>
      )}
      {tiles.length > 0 && (
        <div className="tiles">
          {tiles.map((n) => (
            <Instrument key={n} name={n} kind={kind(n)} state={state} />
          ))}
        </div>
      )}
      {names.length === 0 && (
        <p className="empty">Add a Graph, Display or Image view block on the Flow page and press Run: its instrument shows here.</p>
      )}
    </div>
  );
}
