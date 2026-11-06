import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
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
