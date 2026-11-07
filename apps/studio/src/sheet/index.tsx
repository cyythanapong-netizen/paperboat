import { ReactFlowProvider } from "@xyflow/react";
import { useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from "react";
import {
  CORNER,
  DASH,
  FILL,
  FONT,
  STROKE,
  WEIGHT,
  type Corner,
  type Dash,
  type Fill,
  type IconDef,
  type IconPort,
  type Prim,
  type Stroke,
  type Weight,
} from "@paperboat/shared";
import { CompactBlock, NumberField, Switch } from "@paperboat/shared/components";
import { useStudio } from "../model/types.ts";
import {
  addPrimToIcon,
  applyPlateSize,
  buildLinkedBlockDef,
  CORNER_OPTIONS,
  DASH_OPTIONS,
  FILL_OPTIONS,
  FONT_OPTIONS,
  movePrimBy,
  PLATE_SIZES,
  plateSizeKey,
  PRIM_KINDS,
  removePrimAt,
  sanitizeIconDef,
  setPortPlacement,
  SIDE_OPTIONS,
  SIZE_OPTIONS,
  snapTo5,
  STROKE_OPTIONS,
  syncPortsWithEntries,
  updatePrimTokens,
  WEIGHT_OPTIONS,
  type PlateSizeKey,
} from "./look.ts";
import "./sheet.css";

export type InterfaceSheetProps = { open: boolean; onClose: () => void };

type SheetTab = "interface" | "look";

function initialTabFromUrl(): SheetTab {
  if (typeof window !== "undefined") {
    const q = new URLSearchParams(window.location.search).get("sheetTab");
    if (q === "look") return "look";
  }
  return "interface";
}

function RemoveGlyph() {
  return (
    <svg viewBox="0 0 10 10" aria-hidden="true">
      <path d="M2 2l6 6M8 2L2 8" />
    </svg>
  );
}

const GRID_STEPS = Array.from({ length: 21 }, (_, i) => i * 5);

function primBounds(p: Prim): { x: number; y: number; w: number; h: number } {
  switch (p.t) {
    case "line":
    case "poly": {
      const xs = p.p.map((pt) => pt[0]);
      const ys = p.p.map((pt) => pt[1]);
      const minX = Math.min(...xs);
      const maxX = Math.max(...xs);
      const minY = Math.min(...ys);
      const maxY = Math.max(...ys);
      return { x: minX - 3, y: minY - 3, w: Math.max(6, maxX - minX + 6), h: Math.max(6, maxY - minY + 6) };
    }
    case "rect":
    case "ellipse":
    case "arc":
      return { x: p.x - 2, y: p.y - 2, w: p.w + 4, h: p.h + 4 };
    case "text":
      return { x: p.x - 14, y: p.y - 8, w: 28, h: 16 };
  }
}

export function InterfaceSheet({ open, onClose }: InterfaceSheetProps) {
  const api = useStudio();
  const [tab, setTab] = useState<SheetTab>(initialTabFromUrl);
  const [draftIcon, setDraftIcon] = useState<IconDef>(() =>
    syncPortsWithEntries(api.model.icon, api.model.entries),
  );
  const [selectedIdx, setSelectedIdx] = useState<number>(0);
  const [savedToast, setSavedToast] = useState(false);
  const [, setTick] = useState(0);
  const dragRef = useRef<{ idx: number; startX: number; startY: number; orig: Prim } | null>(null);
  const svgRef = useRef<SVGSVGElement>(null);

  useEffect(() => {
    setDraftIcon(syncPortsWithEntries(api.model.icon, api.model.entries));
  }, [api.model.icon, api.model.entries]);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onClose();
      } else if (
        tab === "look" &&
        (e.key === "Delete" || e.key === "Backspace") &&
        !(e.target instanceof HTMLInputElement)
      ) {
        setDraftIcon((prev) => {
          if (selectedIdx < 0 || selectedIdx >= prev.g.length) return prev;
          const next = removePrimAt(prev, selectedIdx);
          setSelectedIdx(Math.max(0, next.g.length - 1));
          return next;
        });
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open, onClose, tab, selectedIdx]);

  const selectedPrim: Prim | undefined = draftIcon.g[selectedIdx];
  const inputs = api.model.entries.filter((e) => e.kind === "input");
  const outputs = api.model.entries.filter((e) => e.kind === "output");
  const linkedBlockDef = buildLinkedBlockDef(api.model, draftIcon);
  const shortModel = api.model.name.split(".").pop() ?? api.model.name;
  const folderHint = `${shortModel.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toLowerCase()}-cpp/`;

  const svgPoint = (e: ReactPointerEvent<SVGElement>): [number, number] => {
    const svg = svgRef.current;
    if (!svg) return [50, 50];
    const rect = svg.getBoundingClientRect();
    const x = ((e.clientX - rect.left) / Math.max(1, rect.width)) * 100;
    const y = ((e.clientY - rect.top) / Math.max(1, rect.height)) * 100;
    return [snapTo5(x), snapTo5(y)];
  };

  const onPrimPointerDown = (e: ReactPointerEvent<SVGElement>, idx: number) => {
    e.stopPropagation();
    setSelectedIdx(idx);
    const prim = draftIcon.g[idx];
    if (!prim) return;
    const [sx, sy] = svgPoint(e);
    dragRef.current = { idx, startX: sx, startY: sy, orig: structuredClone(prim) };
    (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
  };

  const onCanvasPointerMove = (e: ReactPointerEvent<SVGSVGElement>) => {
    const d = dragRef.current;
    if (!d) return;
    const [cx, cy] = svgPoint(e);
    const dx = cx - d.startX;
    const dy = cy - d.startY;
    const moved = movePrimBy(d.orig, dx, dy);
    setDraftIcon((prev) => ({
      ...prev,
      g: prev.g.map((item, i) => (i === d.idx ? moved : item)),
    }));
  };

  const onCanvasPointerUp = () => {
    dragRef.current = null;
  };

  const patchSelected = (patch: Parameters<typeof updatePrimTokens>[1]) => {
    if (!selectedPrim) return;
    setDraftIcon((prev) => ({
      ...prev,
      g: prev.g.map((item, i) => (i === selectedIdx ? updatePrimTokens(item, patch) : item)),
    }));
  };

  const handleAddPrim = (kind: Prim["t"]) => {
    setDraftIcon((prev) => {
      const next = addPrimToIcon(prev, kind, {
        stroke: selectedPrim?.stroke ?? "ink",
        weight: selectedPrim?.weight ?? "normal",
        dash: selectedPrim?.dash ?? "solid",
        fill: selectedPrim?.fill ?? "none",
      });
      setSelectedIdx(next.g.length - 1);
      return next;
    });
  };

  const handleDeleteSelected = () => {
    setDraftIcon((prev) => {
      if (selectedIdx < 0 || selectedIdx >= prev.g.length) return prev;
      const next = removePrimAt(prev, selectedIdx);
      setSelectedIdx(Math.max(0, next.g.length - 1));
      return next;
    });
  };

  const handleSaveLook = async () => {
    const clean = sanitizeIconDef(draftIcon, api.model.entries);
    setDraftIcon(clean);
    await api.setIcon(clean);
    setSavedToast(true);
    setTimeout(() => setSavedToast(false), 1200);
    setTick((t) => t + 1);
  };

  return (
    <aside
      className={`studio-sheet${open ? " open" : ""}`}
      aria-label="Interface and Look"
      aria-hidden={!open}
      inert={!open}
      data-own-keys
    >
      <header className="studio-sheet-head">
        <h2 className="studio-sheet-title">{shortModel}</h2>
        <Switch<SheetTab>
          label="Tab"
          value={tab}
          onChange={setTab}
          options={[
            { value: "interface", label: "interface", hint: "Station properties and causal entry points" },
            { value: "look", label: "look", hint: "Custom block icon and port placement" },
          ]}
        />
        <button type="button" className="studio-look-key" title="Close (Alt+V or Esc)" onClick={onClose}>
          Close
        </button>
      </header>

      <div className="studio-sheet-body">
        {tab === "interface" ? (
          <>
            <section className="studio-sheet-section" aria-label="Station properties">
              <div className="studio-sheet-subhead">Station properties</div>
              {api.model.params.length === 0 ? (
                <p className="studio-handoff-note">No top-level parameters yet. Use = in a component popover.</p>
              ) : (
                api.model.params.map((p) => (
                  <div key={p.id} className="studio-prop-row">
                    <span className="studio-prop-name">{p.id}</span>
                    {p.type === "number" ? (
                      <NumberField
                        className="studio-prop-input"
                        aria-label={`Station property ${p.id}`}
                        value={p.default}
                        onChange={(v) => {
                          void api.setParam(p.id, String(v)).then(() => setTick((t) => t + 1));
                        }}
                      />
                    ) : (
                      <input
                        className="studio-prop-input"
                        aria-label={`Station property ${p.id}`}
                        value={String(p.default)}
                        onChange={(e) => {
                          void api.setParam(p.id, e.target.value).then(() => setTick((t) => t + 1));
                        }}
                      />
                    )}
                    <span className="studio-prop-unit">{p.type === "number" ? (p.unit ?? "") : p.type}</span>
                  </div>
                ))
              )}
            </section>

            <section className="studio-sheet-section" aria-label="Entry points">
              <div className="studio-sheet-subhead">Entry points</div>
              <div className="studio-entries-grid">
                <div className="studio-entries-col">
                  <span className="studio-entries-col-head">Inputs</span>
                  {inputs.length === 0 ? (
                    <span className="studio-prop-unit">none</span>
                  ) : (
                    inputs.map((entry) => (
                      <div key={entry.name} className="studio-entry-pill">
                        <span className="studio-entry-tag">in</span>
                        <span className="studio-entry-name" title={entry.target}>
                          {entry.name}
                        </span>
                        {entry.unit && <span className="studio-prop-unit">{entry.unit}</span>}
                        <button
                          type="button"
                          className="studio-remove"
                          title={`Remove ${entry.name}`}
                          onClick={() => void api.removeEntry(entry.name).then(() => setTick((t) => t + 1))}
                        >
                          <RemoveGlyph />
                        </button>
                      </div>
                    ))
                  )}
                </div>
                <div className="studio-entries-col">
                  <span className="studio-entries-col-head">Outputs</span>
                  {outputs.length === 0 ? (
                    <span className="studio-prop-unit">none</span>
                  ) : (
                    outputs.map((entry) => (
                      <div key={entry.name} className="studio-entry-pill">
                        <span className="studio-entry-tag">out</span>
                        <span className="studio-entry-name" title={entry.target}>
                          {entry.name}
                        </span>
                        {entry.unit && <span className="studio-prop-unit">{entry.unit}</span>}
                        <button
                          type="button"
                          className="studio-remove"
                          title={`Remove ${entry.name}`}
                          onClick={() => void api.removeEntry(entry.name).then(() => setTick((t) => t + 1))}
                        >
                          <RemoveGlyph />
                        </button>
                      </div>
                    ))
                  )}
                </div>
              </div>
            </section>

            <section className="studio-sheet-section" aria-label="Linked block preview">
              <div className="studio-sheet-subhead">Linked block preview</div>
              <div className="studio-linked-preview">
                <ReactFlowProvider>
                  <CompactBlock
                    id={shortModel}
                    def={linkedBlockDef}
                    params={{}}
                    selected={false}
                    onExpand={() => {}}
                    popover={null}
                    connectors={api.model.connectors}
                  />
                </ReactFlowProvider>
                <p className="studio-handoff-note">
                  Paperboat opens <code>{folderHint}</code> via File › Backend…
                </p>
              </div>
            </section>
          </>
        ) : (
          <>
            <section className="studio-sheet-section" aria-label="Primitives">
              <div className="studio-sheet-subhead">Primitives (5-unit snap grid)</div>
              <div className="studio-look-toolbar">
                {PRIM_KINDS.map((k) => (
                  <button
                    key={k}
                    type="button"
                    className="studio-look-key"
                    onClick={() => handleAddPrim(k)}
                  >
                    + {k}
                  </button>
                ))}
                <button
                  type="button"
                  className="studio-look-key"
                  disabled={!selectedPrim}
                  onClick={handleDeleteSelected}
                >
                  Delete
                </button>
              </div>

              <div className="studio-look-canvas-wrap">
                <svg
                  ref={svgRef}
                  className="studio-look-canvas"
                  viewBox="0 0 100 100"
                  aria-label="Look 0 to 100 icon canvas"
                  onPointerMove={onCanvasPointerMove}
                  onPointerUp={onCanvasPointerUp}
                  onPointerCancel={onCanvasPointerUp}
                >
                  {GRID_STEPS.flatMap((gx) =>
                    GRID_STEPS.map((gy) => (
                      <circle
                        key={`${gx}-${gy}`}
                        className="studio-look-grid-dot"
                        cx={gx}
                        cy={gy}
                        r={gx % 25 === 0 && gy % 25 === 0 ? 0.9 : 0.45}
                      />
                    )),
                  )}
                  {draftIcon.g.map((p, idx) => {
                    const stroke = STROKE[p.stroke ?? "ink"];
                    const strokeWidth = WEIGHT[p.weight ?? "normal"];
                    const strokeDasharray = DASH[p.dash ?? "solid"];
                    const fill = FILL[p.fill ?? "none"];
                    const common = {
                      stroke,
                      strokeWidth,
                      strokeDasharray,
                      strokeLinecap: "round" as const,
                      strokeLinejoin: "round" as const,
                      onPointerDown: (e: ReactPointerEvent<SVGElement>) => onPrimPointerDown(e, idx),
                      style: { cursor: "move" },
                    };
                    switch (p.t) {
                      case "line":
                        return (
                          <polyline
                            key={idx}
                            {...common}
                            fill="none"
                            points={p.p.map(([x, y]) => `${x},${y}`).join(" ")}
                          />
                        );
                      case "poly":
                        return (
                          <polygon
                            key={idx}
                            {...common}
                            fill={fill}
                            points={p.p.map(([x, y]) => `${x},${y}`).join(" ")}
                          />
                        );
                      case "rect":
                        return (
                          <rect
                            key={idx}
                            {...common}
                            fill={fill}
                            x={p.x}
                            y={p.y}
                            width={p.w}
                            height={p.h}
                            rx={CORNER[p.corner ?? "0"]}
                          />
                        );
                      case "ellipse":
                        return (
                          <ellipse
                            key={idx}
                            {...common}
                            fill={fill}
                            cx={p.x + p.w / 2}
                            cy={p.y + p.h / 2}
                            rx={p.w / 2}
                            ry={p.h / 2}
                          />
                        );
                      case "arc": {
                        const [cx, cy, rx, ry] = [p.x + p.w / 2, p.y + p.h / 2, p.w / 2, p.h / 2];
                        const pt = (a: number) =>
                          `${+(cx + rx * Math.cos((a * Math.PI) / 180)).toFixed(2)},${+(
                            cy -
                            ry * Math.sin((a * Math.PI) / 180)
                          ).toFixed(2)}`;
                        const sweep = (((p.a1 - p.a0) % 360) + 360) % 360;
                        return (
                          <path
                            key={idx}
                            {...common}
                            fill={fill}
                            d={`M${pt(p.a0)}A${rx},${ry} 0 ${sweep > 180 ? 1 : 0} 0 ${pt(p.a1)}`}
                          />
                        );
                      }
                      case "text":
                        return (
                          <text
                            key={idx}
                            x={p.x}
                            y={p.y}
                            fill={stroke}
                            textAnchor="middle"
                            dominantBaseline="central"
                            fontFamily={FONT[p.font ?? "sans"]}
                            fontSize={p.size ?? 12}
                            style={{ cursor: "move" }}
                            onPointerDown={(e) => onPrimPointerDown(e, idx)}
                          >
                            {p.s}
                          </text>
                        );
                    }
                  })}
                  {selectedPrim && (() => {
                    const b = primBounds(selectedPrim);
                    return <rect className="studio-look-sel-box" x={b.x} y={b.y} width={b.w} height={b.h} />;
                  })()}
                </svg>
              </div>
            </section>

            <section className="studio-sheet-section" aria-label="Style tokens">
              <div className="studio-sheet-subhead">Tokens (§3)</div>
              <div className="studio-look-choices">
                <Switch<PlateSizeKey>
                  label="Plate"
                  value={plateSizeKey(draftIcon)}
                  onChange={(sz) => setDraftIcon((prev) => applyPlateSize(prev, sz))}
                  options={PLATE_SIZES.map((s) => ({ value: s, label: s }))}
                />
                <Switch<Stroke>
                  label="Stroke"
                  value={selectedPrim?.stroke ?? "ink"}
                  onChange={(stroke) => patchSelected({ stroke })}
                  options={STROKE_OPTIONS}
                />
                <Switch<Weight>
                  label="Weight"
                  value={selectedPrim?.weight ?? "normal"}
                  onChange={(weight) => patchSelected({ weight })}
                  options={WEIGHT_OPTIONS}
                />
                <Switch<Fill>
                  label="Fill"
                  value={selectedPrim?.fill ?? "none"}
                  onChange={(fill) => patchSelected({ fill })}
                  options={FILL_OPTIONS}
                />
                <Switch<Dash>
                  label="Dash"
                  value={selectedPrim?.dash ?? "solid"}
                  onChange={(dash) => patchSelected({ dash })}
                  options={DASH_OPTIONS}
                />
                {selectedPrim?.t === "rect" && (
                  <Switch<Corner>
                    label="Corner"
                    value={selectedPrim.corner ?? "0"}
                    onChange={(corner) => patchSelected({ corner })}
                    options={CORNER_OPTIONS}
                  />
                )}
                {selectedPrim?.t === "text" && (
                  <>
                    <Switch<"sans" | "mono">
                      label="Font"
                      value={selectedPrim.font ?? "sans"}
                      onChange={(font) => patchSelected({ font })}
                      options={FONT_OPTIONS}
                    />
                    <Switch<"12" | "12.5" | "13" | "15" | "30">
                      label="Size"
                      value={String(selectedPrim.size ?? 12) as "12" | "12.5" | "13" | "15" | "30"}
                      onChange={(sz) => patchSelected({ size: Number(sz) as 12 | 12.5 | 13 | 15 | 30 })}
                      options={SIZE_OPTIONS}
                    />
                    <div className="studio-look-port-row">
                      <span className="studio-prop-unit">Text</span>
                      <input
                        className="studio-prop-input"
                        value={selectedPrim.s}
                        aria-label="Primitive text"
                        onChange={(e) => patchSelected({ s: e.target.value })}
                      />
                    </div>
                  </>
                )}
              </div>
            </section>

            <section className="studio-sheet-section" aria-label="Port placement">
              <div className="studio-sheet-subhead">Port placement</div>
              {(draftIcon.ports ?? []).length === 0 ? (
                <span className="studio-prop-unit">No entry ports</span>
              ) : (
                (draftIcon.ports ?? []).map((port) => (
                  <div key={port.id} className="studio-look-port-row">
                    <span className="studio-prop-name">{port.id}</span>
                    <Switch<IconPort["side"]>
                      label="Side"
                      value={port.side}
                      onChange={(side) =>
                        setDraftIcon((prev) => setPortPlacement(prev, api.model.entries, port.id, side, port.at))
                      }
                      options={SIDE_OPTIONS}
                    />
                    <NumberField
                      className="studio-look-at-input"
                      aria-label={`Port ${port.id} at`}
                      value={port.at}
                      onChange={(at) =>
                        setDraftIcon((prev) => setPortPlacement(prev, api.model.entries, port.id, port.side, at))
                      }
                    />
                  </div>
                ))
              )}
            </section>

            <section className="studio-sheet-section" aria-label="Theme preview">
              <div className="studio-sheet-subhead">Preview (light & dark)</div>
              <div className="studio-preview-pair">
                <div className="studio-preview-tile" data-theme="light">
                  <ReactFlowProvider>
                    <CompactBlock
                      id={`${shortModel}-light`}
                      def={linkedBlockDef}
                      params={{}}
                      selected={false}
                      onExpand={() => {}}
                      popover={null}
                      connectors={api.model.connectors}
                    />
                  </ReactFlowProvider>
                  <span className="studio-preview-caption">light</span>
                </div>
                <div className="studio-preview-tile" data-theme="dark">
                  <ReactFlowProvider>
                    <CompactBlock
                      id={`${shortModel}-dark`}
                      def={linkedBlockDef}
                      params={{}}
                      selected={false}
                      onExpand={() => {}}
                      popover={null}
                      connectors={api.model.connectors}
                    />
                  </ReactFlowProvider>
                  <span className="studio-preview-caption">dark</span>
                </div>
              </div>
            </section>

            <footer className="studio-look-foot">
              <span className="studio-prop-unit">
                {savedToast ? "Saved Icon annotation" : `${draftIcon.g.length} shapes · ${plateSizeKey(draftIcon)}`}
              </span>
              <button type="button" className="studio-save-key" onClick={() => void handleSaveLook()}>
                Save
              </button>
            </footer>
          </>
        )}
      </div>
    </aside>
  );
}
