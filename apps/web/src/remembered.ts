// Per-viewer conveniences kept in this browser when it can (a private window keeps nothing): the
// page and mode last shown, the theme, which drawers are open and how big.

import { useState } from "react";
import type { Drawer } from "./Shell";

/** A per-viewer convenience, kept in this browser when it can be. */
export function remembered<T extends string>(key: string, fallback: T, allowed: readonly T[]): [T, (v: T) => void] {
  const [value, setValue] = useState<T>(() => {
    try {
      const v = localStorage.getItem(key) as T | null;
      return v && allowed.includes(v) ? v : fallback;
    } catch {
      return fallback;
    }
  });
  const set = (v: T) => {
    setValue(v);
    try {
      localStorage.setItem(key, v);
    } catch {
      /* private window: not kept */
    }
  };
  return [value, set];
}

/** A drawer's open state and size, kept in this browser. */
export function useDrawer(key: string, open: boolean, size: number, side: Drawer["side"]): Drawer & { setOpen: (o: boolean) => void } {
  const [isOpen, setIsOpen] = remembered<"open" | "closed">(`paperboat.${key}`, open ? "open" : "closed", ["open", "closed"]);
  const [px, setPx] = useState(() => {
    try {
      return Number(localStorage.getItem(`paperboat.${key}Size`)) || size;
    } catch {
      return size;
    }
  });
  return {
    side,
    open: isOpen === "open",
    size: px,
    setOpen: (o) => setIsOpen(o ? "open" : "closed"),
    onChange: (o, next) => {
      setIsOpen(o ? "open" : "closed");
      if (!next) return;
      setPx(next);
      try {
        localStorage.setItem(`paperboat.${key}Size`, String(Math.round(next)));
      } catch {
        /* private window: not kept */
      }
    },
  };
}
