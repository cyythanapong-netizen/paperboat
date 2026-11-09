import { createContext } from "react";
import type { ParamDef, ParamValue, Variables } from "./protocol";
import type { Use } from "./bindings";

/**
 * Variables (#36): named, typed values used by reference in block params and station
 * properties. The list lives in the App (the backend's defaults, then the user's own); the
 * engine resolves it and says what each comes to (`values`), or why it doesn't hold.
 */
export type VarsApi = {
  list: Variables;
  values: Record<string, number | boolean | string>;
  error: { text: string; at?: string } | null;
  /** Names of the backend's defaults (Reset instead of Delete). */
  defaults: Variables;
  locked: boolean;
  setList: (next: Variables) => void;
  rename: (from: string, to: string) => void;
  remove: (name: string) => void;
  uses: (name: string) => Use[];
  /** Open the sheet at `name`. */
  reveal: (name: string) => void;
  /** A new variable from a field's value; returns its name. */
  promote: (def: ParamDef, value: ParamValue) => string;
};
export const VarsContext = createContext<VarsApi | null>(null);
