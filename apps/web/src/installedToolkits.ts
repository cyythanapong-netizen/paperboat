// Toolkits installed into a release (pb#353): `paperboat toolkit install` bundles a toolkit's web part
// into one ES module, `paperboat start` lists them at /__paperboat/toolkits, and the app imports each at
// startup. A bundle leaves React to the app: its imports of react are pointed at the app's own copy.
// (In a checkout the dev server bundles engine/toolkit/*/web at build time instead: instruments.ts.)
import * as React from "react";
import * as ReactDOM from "react-dom";
import * as JsxRuntime from "react/jsx-runtime";
import { toolkitWebHost, type ToolkitWebHost } from "./instruments";
import { linkShared } from "./linkShared";

type Listed = { name: string; js: string; css?: string };

const modules: Record<string, Record<string, unknown>> = { react: React, "react-dom": ReactDOM, "react/jsx-runtime": JsxRuntime };

/** A module that re-exports the app's copy of `name`, as a blob URL a bundle can import. */
function shim(name: string): string {
  const mod = modules[name];
  const keys = Object.keys(mod).filter((k) => k !== "default" && /^[A-Za-z_$][\w$]*$/.test(k));
  const src = [
    `const m = globalThis.__paperboatModules[${JSON.stringify(name)}];`,
    "export default m.default ?? m;",
    ...keys.map((k) => `export const ${k} = m[${JSON.stringify(k)}];`),
  ].join("\n");
  return URL.createObjectURL(new Blob([src], { type: "text/javascript" }));
}

/** Import the installed toolkits' web parts and register them. Never throws: a broken one is skipped. */
export async function loadInstalledToolkits(): Promise<void> {
  let list: Listed[];
  try {
    const res = await fetch("/__paperboat/toolkits");
    if (!res.ok || !res.headers.get("content-type")?.includes("json")) return;  // the dev server, or an older paperboat
    list = await res.json();
  } catch {
    return;
  }
  if (!list.length) return;
  (globalThis as { __paperboatModules?: unknown }).__paperboatModules = modules;
  const urls = Object.fromEntries(Object.keys(modules).map((name) => [name, shim(name)]));
  for (const t of list) {
    try {
      if (t.css) {
        const link = document.createElement("link");
        link.rel = "stylesheet";
        link.href = t.css;
        document.head.append(link);
      }
      const source = await (await fetch(t.js)).text();
      const url = URL.createObjectURL(new Blob([linkShared(source, urls)], { type: "text/javascript" }));
      const mod = (await import(/* @vite-ignore */ url)) as { register?: (host: ToolkitWebHost) => void };
      if (typeof mod.register === "function") mod.register(toolkitWebHost);
    } catch (error) {
      console.warn(`toolkit ${t.name}: its web part did not load`, error);
    }
  }
}
