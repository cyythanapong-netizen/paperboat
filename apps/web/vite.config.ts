import { defineConfig, type Plugin } from "vite";
import { execSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import fs from "node:fs";
import react from "@vitejs/plugin-react";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

function toolkitInstaller(): Plugin {
  return {
    name: "toolkit-installer",
    configureServer(server) {
      const runCommands = (commands: string[]) => {
        const output: string[] = [];
        for (const command of commands) output.push(execSync(command, { cwd: root, encoding: "utf8", stdio: "pipe" }));
        return output.join("\n");
      };

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
            const output = runCommands([
              "python3 scripts/install_toolkit.py opencv",
              "cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Debug",
              "cmake --build engine/build",
            ]);
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify({ ok: true, output }));
          } catch (err) {
            const e = err as { stdout?: string; stderr?: string; message?: string };
            res.statusCode = 500;
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify({ ok: false, output: [e.stdout, e.stderr, e.message].filter(Boolean).join("\n") }));
          }
        });
      });

      server.middlewares.use("/api/uninstall-toolkit", (req, res) => {
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
            const dir = path.join(root, "engine", "toolkit", name);
            if (fs.existsSync(dir)) fs.rmSync(dir, { recursive: true, force: true });
            const output = runCommands([
              "cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Debug",
              "cmake --build engine/build",
            ]);
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify({ ok: true, output }));
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