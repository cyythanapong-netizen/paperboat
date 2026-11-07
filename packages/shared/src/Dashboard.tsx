import type { ReactNode } from "react";
import type { DashboardLayout } from "./protocol.ts";

/**
 * Pure Dashboard shell and presentation types.
 * Web-specific slot content and live engine state stay in apps/web.
 */
export type DashboardShellProps = {
  layout?: DashboardLayout;
  gridOn?: boolean;
  children?: ReactNode;
};

export function DashboardShell({ layout: _layout, gridOn = true, children }: DashboardShellProps) {
  return (
    <div className={`dashboard-shell${gridOn ? " grid-on" : ""}`}>
      {children}
    </div>
  );
}
