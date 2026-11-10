// A backend whose descriptor has no starter [graph]: the Flow page opens with only the
// blocks its station objects spawn (their linked blocks), and with nothing at all when the
// station is empty. Neither is an error. The backend is engine/tests/sdk-backend's
// empty-main.cpp, built by the same `paperboat build` as the smoke test's backend.
import { spawn, type ChildProcess } from "node:child_process";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test, type Page } from "@playwright/test";
import { EMPTY_BACKEND_PORT, STATION_BACKEND_PORT } from "./env";

const here = path.dirname(fileURLToPath(import.meta.url));
const backend = path.resolve(here, "../../../engine/tests/sdk-backend/build/paperboat-sdk-backend-empty");
const url = (port: number) => `/?backend=${encodeURIComponent(`ws://localhost:${port}`)}`;

/** Start the fixture backend on `port`, or fail with what it said if it never listens. */
function start(port: number, emptyStation: boolean): Promise<ChildProcess> {
  return new Promise((resolve, reject) => {
    const env = { ...process.env };
    if (emptyStation) env.PAPERBOAT_EMPTY_STATION = "1";
    else delete env.PAPERBOAT_EMPTY_STATION;
    const proc = spawn(backend, ["--port", String(port)], { env, stdio: ["ignore", "pipe", "pipe"] });
    let stderr = "";
    proc.stderr?.on("data", (d) => (stderr += String(d)));
    const timer = setTimeout(() => {
      proc.kill();
      reject(new Error(`the empty-Flow backend did not listen on ${port}\n${stderr}`));
    }, 20_000);
    const poll = () => {
      const socket = net.createConnection({ port, host: "127.0.0.1" });
      socket.once("connect", () => {
        socket.destroy();
        clearTimeout(timer);
        resolve(proc);
      });
      socket.once("error", () => {
        socket.destroy();
        setTimeout(poll, 100);
      });
    };
    poll();
    proc.once("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`the empty-Flow backend exited (${code}) before listening on ${port}\n${stderr}`));
    });
    proc.once("error", (err) => {
      clearTimeout(timer);
      reject(new Error(`cannot start ${backend}: ${err.message}`));
    });
  });
}

/** Open the app on a port, wait for the backend, and collect uncaught page errors. */
async function open(page: Page, port: number): Promise<Error[]> {
  const errors: Error[] = [];
  page.on("pageerror", (err) => errors.push(err));
  await page.goto(url(port));
  await expect(page.getByRole("banner").getByRole("status")).toHaveText("cpp-sdk-backend-empty", {
    useInnerText: true,
    timeout: 20_000,
  });
  return errors;
}

test.describe("a backend with no starter graph", () => {
  let station: ChildProcess;
  let empty: ChildProcess;

  test.beforeAll(async () => {
    [station, empty] = await Promise.all([start(STATION_BACKEND_PORT, false), start(EMPTY_BACKEND_PORT, true)]);
  });

  test.afterAll(() => {
    station?.kill();
    empty?.kill();
  });

  test("opens an empty Flow page, without an error", async ({ page }) => {
    const errors = await open(page, EMPTY_BACKEND_PORT);
    await expect(page.locator(".react-flow__node")).toHaveCount(0);
    await expect(page.getByText("or double-click to add a block")).toBeVisible();
    // No error in the Console either: the header says "Console", not "1 error".
    await expect(page.getByRole("banner").getByRole("button", { name: "Console" })).toBeVisible();
    expect(errors).toEqual([]);
  });

  test("shows only the blocks its station objects spawn, and no default layout", async ({ page }) => {
    const errors = await open(page, STATION_BACKEND_PORT);
    await expect(page.locator(".react-flow__node")).toHaveCount(1);
    await expect(page.locator(".react-flow__node")).toContainText("Thermometer");
    await expect(page.getByText("or double-click to add a block")).toHaveCount(0);
    await expect(page.getByRole("banner").getByRole("button", { name: "Console" })).toBeVisible();
    expect(errors).toEqual([]);
  });
});
