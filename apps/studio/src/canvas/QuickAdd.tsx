// `/` on the canvas: type a part, pick it, and it lands at the centre, or, with a wire selected,
// splices into that wire. The line shown before you confirm is the edit it will make.

import { useEffect, useRef, useState } from "react";
import { search } from "../library/classes.ts";

export type SplicePlan = { line: string; apply(): Promise<void> } | { why: string };

type Props = {
  names: string[];
  /** The selected wire, as "R1.n - L1.p", when there is one. */
  wire?: string;
  plan(cls: string): Promise<SplicePlan>;
  onPlace(cls: string): void;
  onClose(): void;
};

export function QuickAdd({ names, wire, plan, onPlace, onClose }: Props) {
  const [query, setQuery] = useState("");
  const [at, setAt] = useState(0);
  const [preview, setPreview] = useState<SplicePlan | null>(null);
  const input = useRef<HTMLInputElement>(null);
  const hits = search(names, query, 12);
  const picked = hits[Math.min(at, hits.length - 1)];
  useEffect(() => input.current?.focus(), []);
  useEffect(() => {
    setPreview(null);
    if (!wire || !picked) return;
    let live = true;
    plan(picked).then((p) => live && setPreview(p), (e: Error) => live && setPreview({ why: e.message }));
    return () => {
      live = false;
    };
  }, [wire, picked, plan]);

  const confirm = async () => {
    if (!picked) return;
    if (wire) {
      if (preview && "apply" in preview) await preview.apply();
      else return;
    } else onPlace(picked);
    onClose();
  };

  return (
    <div className="quick-add" role="dialog" aria-label="Add a part">
      <input
        ref={input}
        value={query}
        placeholder={wire ? `Insert into ${wire}…` : "Add a part…"}
        aria-label="Part name"
        onChange={(e) => {
          setQuery(e.target.value);
          setAt(0);
        }}
        onKeyDown={(e) => {
          if (e.key === "Escape") onClose();
          else if (e.key === "ArrowDown") (e.preventDefault(), setAt((a) => Math.min(a + 1, hits.length - 1)));
          else if (e.key === "ArrowUp") (e.preventDefault(), setAt((a) => Math.max(a - 1, 0)));
          else if (e.key === "Enter") (e.preventDefault(), void confirm());
        }}
      />
      <ul>
        {hits.map((h, i) => (
          <li key={h} className={i === at ? "on" : undefined} onMouseEnter={() => setAt(i)} onMouseDown={(e) => (e.preventDefault(), setAt(i), void confirm())}>
            <b>{h.slice(h.lastIndexOf(".") + 1)}</b>
            <span>{h.slice(0, h.lastIndexOf("."))}</span>
          </li>
        ))}
        {!hits.length && <li className="none">{query ? "nothing matches" : names.length ? "type a part name" : "reading the library…"}</li>}
      </ul>
      {wire && picked && (
        <p className={`quick-foot ${preview && "why" in preview ? "refused" : ""}`} data-testid="splice-line">
          {preview ? ("line" in preview ? preview.line : preview.why) : "…"}
        </p>
      )}
    </div>
  );
}
