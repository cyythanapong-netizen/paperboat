import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import type { IncomingMessage, ServerResponse } from "node:http";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { defineConfig, searchForWorkspaceRoot, type Plugin } from "vite";
import react from "@vitejs/plugin-react";

const here = fileURLToPath(new URL(".", import.meta.url));
const run = promisify(execFile);

/**
 * Dev-server equivalent of the installed app's `/__paperboat/backends`: ask the CLI for what
 * is running, so the Backend dialog can list it instead of asking for a port by hand.
 */
function backendList(): Plugin {
  const handler = (_request: IncomingMessage, response: ServerResponse) => {
    const root = join(here, "..", ".."); // apps/web → the checkout
    const bin = process.env.PAPERBOAT_BIN || join(root, "bin", "paperboat");
    response.setHeader("Content-Type", "application/json");
    run(bin, ["list", "--json"], { cwd: root, timeout: 8000 })
      .then(({ stdout }) => response.end(stdout || "[]"))
      .catch(() => {
        response.statusCode = 503;
        response.end("[]");
      });
  };
  const install = (server: { middlewares: { use: (path: string, fn: typeof handler) => void } }) => {
    server.middlewares.use("/__paperboat/backends", handler);
  };
  return {
    name: "paperboat-backends",
    configureServer: install,
    configurePreviewServer: install,
  };
}

/** The folder holding a package's package.json, from a file `require` resolved inside it. */
function packageRoot(file: string): string {
  let dir = dirname(file);
  while (!existsSync(join(dir, "package.json")) && dirname(dir) !== dir) dir = dirname(dir);
  return existsSync(join(dir, "package.json")) ? dir : "";
}

/**
 * Folders outside this workspace that the dev server must serve files from. @paperboat/shared is a
 * `file:` dependency, so usually a link to a sibling checkout, and its tokens.css pulls the Geist fonts
 * from that checkout's own node_modules: real paths outside the workspace root, which Vite answers with
 * 403 unless allowed. Resolved like Node does (through links, to real paths), so it holds wherever the
 * package and its fonts live; anything that doesn't resolve is simply left out.
 */
function sharedFolders(): string[] {
  const folders: string[] = [];
  const add = (folder: string) => {
    if (folder && !folders.includes(folder)) folders.push(folder);
  };
  try {
    const shared = packageRoot(createRequire(import.meta.url).resolve("@paperboat/shared"));
    add(shared);
    if (!shared) return folders;
    const fromShared = createRequire(join(shared, "package.json"));
    for (const font of ["@fontsource/geist-sans", "@fontsource/geist-mono"]) {
      try {
        add(packageRoot(fromShared.resolve(font)));
      } catch {
        // not installed there: the app's own copy (inside the workspace) is used
      }
    }
  } catch {
    // @paperboat/shared not installed: nothing to add
  }
  return folders;
}

export default defineConfig({
  plugins: [react(), backendList()],
  // @paperboat/shared is a linked sibling with its own node_modules; keep one React and Three.
  resolve: {
    dedupe: ["react", "react-dom", "@xyflow/react", "three"],
    // What a toolkit's web code imports from the app (types only, erased at build; see src/host.ts).
    alias: { "@paperboat/host": fileURLToPath(new URL("./src/host.ts", import.meta.url)) },
  },
  server: {
    port: 3047,
    // Replacing the default allow list, so it keeps the workspace root Vite would use.
    fs: { allow: [searchForWorkspaceRoot(here), ...sharedFolders()] },
  },
  preview: {
    port: 3047,
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes("node_modules/three")) return "three";
          if (id.includes("node_modules/@xyflow/react")) return "xyflow";
          return undefined;
        },
      },
    },
  },
});
