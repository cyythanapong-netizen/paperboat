/** Small inline loading spinner for long-running operations (toolkit install/update/uninstall). */
export function Spinner({ label }: { label?: string }) {
  return (
    <span className="spinner" role="status" aria-label={label ?? "Loading"}>
      <svg viewBox="0 0 16 16" aria-hidden="true">
        <circle className="spinner-track" cx="8" cy="8" r="6" />
        <path className="spinner-arc" d="M 8 2 A 6 6 0 0 1 14 8" />
      </svg>
    </span>
  );
}
