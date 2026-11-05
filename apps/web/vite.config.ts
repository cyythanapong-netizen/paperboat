import { defineConfig, type Plugin } from "vite";
import { execSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import react from "@vitejs/plugin-react";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

function toolkitInstaller(): Plugin {
  return {
    name: "toolkit-installer",
    configureServer(server) {
      server.middlewares.use("/api/install-toolkit", (req, res) => {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(JSON.stringify({ error: "method not allowed" }));
          return;
        }
        let body = "";
        req.on("data", (chunk) => (body += chunk));
        req.on("end", () => {
          try {
            const { name } = JSON.parse(body || "{}");
            if (name !== "opencv") throw new Error(`unknown toolkit: ${name}`);
            const output: string[] = [];
            output.push(execSync("python3 scripts/install_toolkit.py opencv", { cwd: root, encoding: "utf8", stdio: "pipe" }));
            output.push(execSync("cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Debug", { cwd: root, encoding: "utf8", stdio: "pipe" }));
            output.push(execSync("cmake --build engine/build", { cwd: root, encoding: "utf8", stdio: "pipe" }));
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify({ ok: true, output: output.join("\n") }));
          } catch (err) {
            const e = err as { stdout?: string; stderr?: string; message?: string };
            res.statusCode = 500;
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify({ ok: false, output: [e.stdout, e.stderr, e.message].filter(Boolean).join("\n") }));
          }
        });
      });
    },
  };
}

export default defineConfig({
  plugins: [react(), toolkitInstaller()],
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