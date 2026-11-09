// End-to-end tests: the web app (Vite dev server) driven in headless Chromium against a real engine backend
// (engine/tests/sdk-backend). Run with `npm run e2e` (root) or `npm run e2e -w @paperboat/web`.
// Needs the engine configured in engine/build (bin/paperboat builds its CLI from there) and a C++ toolchain:
// the backend is built with `paperboat build` on the first run (a few minutes), then reused.
import path from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, devices } from "@playwright/test";
import { BACKEND_PORT, WEB_PORT } from "./env";

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, "..");
const root = path.resolve(web, "../..");
const backend = path.join(root, "engine/tests/sdk-backend");
const sh = (p: string) => `'${p.replace(/'/g, `'\\''`)}'`; // single-quoted for the POSIX shell: spaces, $ and backticks are literal

if (process.platform === "win32")
  throw new Error("the end-to-end suite needs a POSIX shell (bin/paperboat); run it on Linux or macOS");

export default defineConfig({
  testDir: ".",
  outputDir: "test-results",
  timeout: 60_000,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  workers: 1, // one backend: its runs are shared state
  reporter: process.env.CI ? [["list"], ["html", { outputFolder: "playwright-report", open: "never" }]] : "list",
  use: {
    baseURL: `http://localhost:${WEB_PORT}`,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  webServer: [
    {
      name: "backend",
      // Build when stale (a no-op otherwise), then exec the backend itself, so stopping the server stops it.
      command: `${sh(path.join(root, "bin/paperboat"))} build ${sh(backend)} && exec ${sh(path.join(backend, "build/paperboat-sdk-backend"))} --port ${BACKEND_PORT}`,
      port: BACKEND_PORT,
      timeout: 15 * 60_000, // the first build compiles the engine core
      reuseExistingServer: false,
      stdout: "pipe",
    },
    {
      name: "web",
      command: `npx vite --port ${WEB_PORT} --strictPort`,
      cwd: web,
      url: `http://localhost:${WEB_PORT}`,
      timeout: 120_000,
      reuseExistingServer: !process.env.CI,
    },
  ],
});
