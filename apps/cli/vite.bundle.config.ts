// The console as one file for the release packages (share/paperboat/cli/main.js): Ink and React
// inlined, so it runs on a bare `node` with no node_modules. `npm run bundle -w @paperboat/cli`.
import { defineConfig } from "vite";

export default defineConfig({
  build: {
    ssr: "src/main.tsx",
    outDir: "bundle",
    emptyOutDir: true,
    target: "node20",
    rollupOptions: { output: { format: "esm", entryFileNames: "main.js", inlineDynamicImports: true } },
  },
  ssr: { noExternal: true, target: "node" },
});
