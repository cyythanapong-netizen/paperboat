import { existsSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, searchForWorkspaceRoot } from "vite";
import react from "@vitejs/plugin-react";

const here = fileURLToPath(new URL(".", import.meta.url));

/** The folder holding a package's package.json, from a file `require` resolved inside it. */
function packageRoot(file: string): string {
  let dir = dirname(file);
  while (!existsSync(join(dir, "package.json")) && dirname(dir) !== dir) dir = dirname(dir);
  return dir;
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
  try {
    const shared = packageRoot(createRequire(import.meta.url).resolve("@paperboat/shared"));
    folders.push(shared);
    const fromShared = createRequire(join(shared, "package.json"));
    for (const font of ["@fontsource/geist-sans", "@fontsource/geist-mono"]) {
      try {
        folders.push(packageRoot(fromShared.resolve(font)));
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
  plugins: [react()],
  // @paperboat/shared is a linked sibling with its own node_modules; keep one React.
  resolve: {
    dedupe: ["react", "react-dom", "@xyflow/react"],
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
