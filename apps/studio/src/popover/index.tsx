import { useEffect, useRef, useState } from "react";
import { NumberField } from "@paperboat/shared/components";
import { useStudio } from "../model/types.ts";
import {
  bindParamToTopLevel,
  commitParamValue,
  exposeComponentPort,
  paramUnit,
  parseParamBinding,
  portActionFor,
  resolveParamNumber,
  unbindParam,
} from "./popover.ts";
import "./popover.css";

export type ComponentPopoverProps = { component: string; onClose: () => void };

export function ComponentPopover({ component, onClose }: ComponentPopoverProps) {
  const api = useStudio();
  const rootRef = useRef<HTMLDivElement>(null);
  const [bindingParam, setBindingParam] = useState<string | null>(null);
  const [bindName, setBindName] = useState("");
  const [, setTick] = useState(0);

  const comp = api.model.components.find((c) => c.name === component);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        if (bindingParam) {
          setBindingParam(null);
        } else {
          onClose();
        }
      }
    };
    const onPointerDown = (e: PointerEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node | null)) {
        onClose();
      }
    };
    document.addEventListener("keydown", onKey);
    document.addEventListener("pointerdown", onPointerDown);
    return () => {
      document.removeEventListener("keydown", onKey);
      document.removeEventListener("pointerdown", onPointerDown);
    };
  }, [bindingParam, onClose]);

  if (!comp) return null;

  const shortClass = comp.class.split(".").pop() ?? comp.class;
  const paramEntries = Object.entries(comp.params);
  const left = Math.max(16, comp.placement.x + 76);
  const top = Math.max(56, comp.placement.y);

  const startBind = (paramId: string) => {
    setBindingParam(paramId);
    setBindName(`${paramId}_load`);
  };

  const submitBind = async (paramId: string, rawVal: string) => {
    const ok = await bindParamToTopLevel(api, comp.name, paramId, bindName, rawVal);
    if (ok) {
      setBindingParam(null);
      setTick((t) => t + 1);
    }
  };

  return (
    <div
      ref={rootRef}
      className="studio-popover"
      role="dialog"
      aria-label={`${comp.name} parameters`}
      style={{ left: `${left}px`, top: `${top}px` }}
      data-own-keys
    >
      <header className="studio-popover-head">
        <span className="studio-popover-name">{comp.name}</span>
        <span className="studio-popover-class" title={comp.class}>
          {shortClass}
        </span>
      </header>

      <div className="studio-popover-section">
        <span className="studio-popover-label">Parameters</span>
        {paramEntries.length === 0 ? (
          <span className="studio-popover-class">No modifiers</span>
        ) : (
          paramEntries.map(([paramId, rawVal]) => {
            const parsed = parseParamBinding(paramId, rawVal);
            const numVal = resolveParamNumber(paramId, rawVal, api.model);
            const unit = paramUnit(paramId, api.model, parsed.bound);

            return (
              <div key={paramId}>
                <div className="studio-param-row">
                  <span className="studio-param-id">{paramId}</span>
                  <div className="studio-param-controls">
                    {parsed.bound ? (
                      <span className="studio-bound-chip">
                        <span className="studio-bound-ref" title={`${paramId} = ${parsed.bound}`}>
                          {parsed.bound}
                        </span>
                        <span className="studio-bound-val">{numVal}</span>
                        {unit && <span className="studio-unit">{unit}</span>}
                        <button
                          type="button"
                          className="studio-key"
                          title={`Unbind ${paramId} (keep ${numVal})`}
                          onClick={() => void unbindParam(api, comp.name, paramId, parsed.bound!).then(() => setTick((t) => t + 1))}
                        >
                          ×
                        </button>
                      </span>
                    ) : (
                      <>
                        <NumberField
                          className="studio-well-input"
                          aria-label={`${comp.name}.${paramId}`}
                          value={numVal}
                          onChange={(v) => {
                            void commitParamValue(api, comp.name, paramId, v).then(() => setTick((t) => t + 1));
                          }}
                        />
                        {unit && <span className="studio-unit">{unit}</span>}
                        <button
                          type="button"
                          className="studio-key"
                          title={`Bind ${paramId} to a top-level Station parameter`}
                          onClick={() => startBind(paramId)}
                        >
                          =
                        </button>
                      </>
                    )}
                  </div>
                </div>
                {bindingParam === paramId && (
                  <form
                    className="studio-bind-inline"
                    onSubmit={(e) => {
                      e.preventDefault();
                      void submitBind(paramId, rawVal);
                    }}
                  >
                    <input
                      className="studio-bind-input"
                      autoFocus
                      spellCheck={false}
                      value={bindName}
                      placeholder={`${paramId}_load`}
                      aria-label={`Top-level parameter name for ${paramId}`}
                      onChange={(e) => setBindName(e.target.value)}
                    />
                    <button type="submit" className="studio-key">
                      Bind
                    </button>
                  </form>
                )}
              </div>
            );
          })
        )}
      </div>

      {comp.ports.length > 0 && (
        <div className="studio-popover-section">
          <span className="studio-popover-label">Ports</span>
          {comp.ports.map((port) => {
            const action = portActionFor(comp, port, api.model.connectors, api.model.entries);
            return (
              <div key={port.id} className="studio-port-row">
                <span className="studio-port-info">
                  <span className="studio-port-name">{port.id}</span>
                  <span className="studio-port-class">{port.class}</span>
                  {action.unit && <span className="studio-unit">{action.unit}</span>}
                </span>
                {action.existingEntry ? (
                  <span className="studio-entry-badge" title={`Exposed as ${action.existingEntry}`}>
                    [ {action.existingEntry} ]
                  </span>
                ) : (
                  <button
                    type="button"
                    className="studio-key"
                    onClick={() => void exposeComponentPort(api, comp, port).then(() => setTick((t) => t + 1))}
                  >
                    {action.buttonLabel}
                  </button>
                )}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
