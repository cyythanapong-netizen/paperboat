import { useEffect } from "react";
import { ownKeys } from "./commands";
import type { ClientMsg } from "./protocol";

/** Forward the keyboard to the backend (e.g. a Key block). Typing in a field is not input. */
export function useBackendKeys(send: (m: ClientMsg) => void) {
  useEffect(() => {
    const forward = (down: boolean) => (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || ownKeys(e.target) || e.repeat || e.defaultPrevented) return; // taken by the app (quick add's /, a menu's arrows)
      if (e.code === "Space") e.preventDefault(); // don't click the focused button
      send({ type: "input", key: e.code, down });
    };
    const onDown = forward(true);
    const onUp = forward(false);
    window.addEventListener("keydown", onDown);
    window.addEventListener("keyup", onUp);
    return () => {
      window.removeEventListener("keydown", onDown);
      window.removeEventListener("keyup", onUp);
    };
  }, [send]);
}
