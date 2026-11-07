// Simulate (contracts §8.2): a local check, not a second Flow page. Stop time, a Simulate key, the
// variables as chips to switch on and off, and the run as a uPlot plot. omc runs it; nothing
// leaves Studio and no block, board or backend is involved.

import { useEffect, useRef, useState } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import type { Actions } from "../store/index.tsx";

type Run = Awaited<ReturnType<Actions["simulate"]>>;

/** The variables worth plotting first: not derivatives, fewest dots, a few of them. */
export function defaultPicks(names: string[], limit = 3): string[] {
  return [...names]
    .filter((n) => !n.startsWith("der("))
    .sort((a, b) => a.split(".").length - b.split(".").length || a.localeCompare(b))
    .slice(0, limit);
}

const token = (name: string) => getComputedStyle(document.documentElement).getPropertyValue(name).trim() || "#888";

function Plot({ run, picks, theme }: { run: Run; picks: string[]; theme: string }) {
  const host = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!host.current) return;
    const grid = { stroke: token("--line"), width: 1 };
    const axis = { stroke: token("--text-muted"), grid, ticks: grid, font: `12px ${token("--font-mono")}` };
    const plot = new uPlot(
      {
        width: host.current.clientWidth || 400,
        height: 180,
        legend: { show: false },
        cursor: { drag: { x: true, y: false } },
        scales: { x: { time: false } },
        axes: [axis, axis],
        series: [{}, ...picks.map((name, i) => ({ label: name, stroke: token(`--trace-${(i % 8) + 1}`), width: 1.5, dash: i >= 4 ? [4, 3] : undefined }))],
      },
      [run.t, ...picks.map((n) => run.series[n] ?? [])] as uPlot.AlignedData,
      host.current,
    );
    const resize = new ResizeObserver(() => host.current && plot.setSize({ width: host.current.clientWidth, height: 180 }));
    resize.observe(host.current);
    return () => {
      resize.disconnect();
      plot.destroy();
    };
  }, [run, picks, theme]);
  return <div className="plot" ref={host} role="img" aria-label={`Plot of ${picks.join(", ")}`} />;
}

export function Simulate({ actions, theme }: { actions: Actions; theme: string }) {
  const [stop, setStop] = useState("1");
  const [run, setRun] = useState<Run | null>(null);
  const [picks, setPicks] = useState<string[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const go = async () => {
    const t = Number(stop);
    if (!(t > 0)) return setError("stop time must be above 0");
    setBusy(true);
    setError("");
    try {
      const result = await actions.simulate(t);
      setRun(result);
      setPicks((p) => (p.filter((n) => result.names.includes(n)).length ? p.filter((n) => result.names.includes(n)) : defaultPicks(result.names)));
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="simulate" aria-label="Simulate">
      <header>
        <label className="lbl" htmlFor="stop-time">Stop time (s)</label>
        <input id="stop-time" className="well" inputMode="decimal" value={stop} onChange={(e) => setStop(e.target.value)} onKeyDown={(e) => e.key === "Enter" && void go()} />
        <button className="key run" disabled={busy} onClick={() => void go()}>
          {busy ? "simulating…" : "Simulate"}
        </button>
      </header>
      {error && <p className="error" role="alert">{error}</p>}
      {run && (
        <>
          <Plot run={run} picks={picks} theme={theme} />
          <div className="vars" role="group" aria-label="Variables">
            {run.names.map((n) => (
              <button key={n} className={`chip-pick${picks.includes(n) ? " on" : ""}`} aria-pressed={picks.includes(n)} onClick={() => setPicks((p) => (p.includes(n) ? p.filter((x) => x !== n) : [...p, n]))}>
                {n}
              </button>
            ))}
          </div>
        </>
      )}
    </section>
  );
}
