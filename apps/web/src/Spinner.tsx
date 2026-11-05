import "./styles.css";

/**
 * A reusable indeterminate spinner. Use it anywhere an async operation is in flight:
 * toolkit install/update, backend connect, catalog load, etc. Size in px (default 14).
 */
export function Spinner({ size = 14, className = "" }: { size?: number; className?: string }) {
  return (
    <span
      className={`spinner ${className}`.trim()}
      style={{ width: size, height: size }}
      role="status"
      aria-label="Loading"
    />
  );
}
