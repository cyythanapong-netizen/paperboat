// omc's answers as data: `{a, "b", {c}, Placement(true,1.0,2.0)}` → nested arrays of text.
// Strings come back unquoted and unescaped; every other atom keeps its spelling (a call such as
// `Placement(…)` is one atom). The same reading as engine/modelica's parse_value.

export type Val = string | Val[];

export function parseValue(text: string): Val {
  let i = 0;
  const ws = () => {
    while (i < text.length && /\s/.test(text[i])) i++;
  };
  const value = (): Val => {
    ws();
    if (i >= text.length) throw new Error("omc answer ended early");
    const c = text[i];
    if (c === "{" || c === "(") {
      const close = c === "{" ? "}" : ")";
      const items: Val[] = [];
      i++;
      ws();
      if (text[i] === close) {
        i++;
        return items;
      }
      for (;;) {
        items.push(value());
        ws();
        if (text[i] === ",") i++;
        else if (text[i] === close) {
          i++;
          return items;
        } else throw new Error(`omc answer: expected , or ${close} at ${i}`);
      }
    }
    if (c === '"') {
      let out = "";
      for (i++; i < text.length && text[i] !== '"'; i++) {
        if (text[i] === "\\" && i + 1 < text.length) {
          i++;
          out += text[i] === "n" ? "\n" : text[i] === "t" ? "\t" : text[i];
        } else out += text[i];
      }
      if (text[i] !== '"') throw new Error("omc answer: unterminated string");
      i++;
      return out;
    }
    // An atom runs to the next , } ) at depth 0; its own ( ) { } and strings nest.
    const start = i;
    let depth = 0;
    for (; i < text.length; i++) {
      const d = text[i];
      if (d === '"') {
        for (i++; i < text.length && text[i] !== '"'; i++) if (text[i] === "\\") i++;
      } else if (d === "(" || d === "{") depth++;
      else if (d === ")" || d === "}") {
        if (depth === 0) break;
        depth--;
      } else if (d === "," && depth === 0) break;
    }
    return text.slice(start, i).trim();
  };
  const v = value();
  ws();
  return v;
}

/** The text of an atom or string, "" for a list. */
export const atom = (v: Val | undefined): string => (typeof v === "string" ? v : "");
export const list = (v: Val | undefined): Val[] => (Array.isArray(v) ? v : []);

/** `Name(a, b, {c, d})` → its name and top-level arguments as text; null when v isn't a call. */
export function parseCall(v: Val | undefined): { name: string; args: string[] } | null {
  const m = /^([A-Za-z_][\w.]*)\s*\(([\s\S]*)\)$/.exec(atom(v));
  if (!m) return null;
  const args: string[] = [];
  let depth = 0;
  let from = 0;
  const body = m[2];
  for (let i = 0; i < body.length; i++) {
    const c = body[i];
    if (c === '"') for (i++; i < body.length && body[i] !== '"'; i++) if (body[i] === "\\") i++;
    if (c === "(" || c === "{") depth++;
    else if (c === ")" || c === "}") depth--;
    else if (c === "," && depth === 0) {
      args.push(body.slice(from, i).trim());
      from = i + 1;
    }
  }
  if (body.trim()) args.push(body.slice(from).trim());
  return { name: m[1], args };
}

/** A Modelica string literal. */
export const quote = (s: string) => `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
