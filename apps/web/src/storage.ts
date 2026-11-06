// The browser's localStorage as JSON. Storage can be blocked or full: reads then find
// nothing and writes are dropped, so whatever it holds lasts for the session only.

/** The value stored under `key`, if there is one and `valid` accepts it. */
export function readStored<T>(key: string, valid: (v: unknown) => v is T): T | undefined {
  try {
    const v = JSON.parse(localStorage.getItem(key) ?? "null");
    return valid(v) ? v : undefined;
  } catch {
    return undefined;
  }
}

/** Keep `value` under `key`; `undefined` (or `null`) forgets it. */
export function writeStored(key: string, value: unknown) {
  try {
    if (value == null) localStorage.removeItem(key);
    else localStorage.setItem(key, JSON.stringify(value));
  } catch {
    /* storage blocked or full: kept for this session only */
  }
}
