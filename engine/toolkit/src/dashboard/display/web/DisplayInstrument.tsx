import type { InstrumentProps } from "@paperboat/host";
import { GRID_BY_SIZE, readout } from "./readout";

/** A display reads out more as its card grows: a longer list, and on L and XL a matrix's corner as a grid. */
export function DisplayInstrument({ name, latest, vector, idle, size }: InstrumentProps) {
  const { dims, type, values, grid } = readout(latest, vector, size);
  const n = GRID_BY_SIZE[size ?? ""];
  const rows = grid && n ? grid.rows.slice(0, n) : undefined;
  const cutColumns = !!grid && (grid.moreColumns || grid.rows.some((r) => r.length > n));
  const cutRows = !!grid && (grid.moreRows || grid.rows.length > n);
  return (
    <figure className={`instrument display${idle ? " idle" : ""}`}>
      <div className="screen">
        {dims && <span className="dims">{dims}</span>}
        {rows ? (
          <table className="matrix-grid" aria-label={`${name}: ${dims} matrix, top-left corner`}>
            <tbody>
              {rows.map((r, i) => (
                <tr key={i}>
                  {r.slice(0, n).map((v, j) => <td key={j}>{v}</td>)}
                  {cutColumns && <td className="more">⋯</td>}
                </tr>
              ))}
              {cutRows && (
                <tr>
                  {rows[0].slice(0, n).map((_, j) => <td key={j} className="more">⋮</td>)}
                  {cutColumns && <td className="more">⋱</td>}
                </tr>
              )}
            </tbody>
          </table>
        ) : (
          <output>{type ? `${type}: ${values}` : values}</output>
        )}
      </div>
      <figcaption>
        <span>{name}</span>
        <span>display</span>
      </figcaption>
    </figure>
  );
}
