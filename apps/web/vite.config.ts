import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

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
