import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Dev server only; `squero-om` serves the built dist/ and squero/0 on 8790.
export default defineConfig({ plugins: [react()], build: { chunkSizeWarningLimit: 800 }, server: { port: 3048 }, preview: { port: 3048 } });
