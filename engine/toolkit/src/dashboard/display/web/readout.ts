/** How many values a Display lists before it shows min, max and mean instead (#24, #349). */
export const SHOWN = 6;

const number = (v: number) => String(+v.toFixed(4));

/** A long list as min, max and mean: what is worth a glance when the values themselves are too many. */
const stats = (v: number[]) => `min ${number(Math.min(...v))}, max ${number(Math.max(...v))}, mean ${number(v.reduce((a, b) => a + b, 0) / v.length)}`;

export interface Readout {
  /** The value's size, "2 × 2" or "3"; absent for a single value or text. */
  dims?: string;
  /** What kind of value: scalar, vector, matrix, points; absent for plain text. */
  type?: string;
  /** The values, cut to a preview so a huge matrix or list never floods the card. */
  values: string;
}

/**
 * What a Display reads out. A vector longer than SHOWN becomes its stats here (with its component labels, `vector`, when it
 * has them). A matrix or point list arrives from the engine already as `N × M matrix: first, values, …`
 * (run.cpp matrix_text / points_text; a big matrix as its stats), so its size is read off that text, not from the data.
 */
export function readout(latest: unknown, vector?: string): Readout {
  if (typeof latest === "number") return { type: "scalar", values: number(latest) };
  if (Array.isArray(latest)) {
    const nums = latest.filter((v): v is number => typeof v === "number");
    // A labelled vector's parts are one per value: keep the labels when it is shown whole.
    const labels = vector?.split(", ");
    const parts = labels?.length === nums.length ? labels : nums.map(number);
    const values = nums.length > SHOWN ? stats(nums) : parts.join(", ");
    return { dims: String(nums.length), type: "vector", values };
  }
  const text = String(latest ?? "–");
  const m = /^(\d+ × \d+) (matrix): (.*)$/s.exec(text) ?? /^(\d+) (points?): (.*)$/s.exec(text);
  return m ? { dims: m[1], type: m[2] === "matrix" ? "matrix" : "points", values: m[3] } : { values: text };
}
