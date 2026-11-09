// The smoke test: the UI and an engine backend work together. Open the app on the sdk-backend, run the
// starter graph it sends (a Number 2 wired into a Graph), and see the results arrive.
import { expect, test } from "@playwright/test";
import { BACKEND_URL } from "./env";

test("connects to a backend, runs its starter graph and shows the results", async ({ page }) => {
  await page.goto(`/?backend=${encodeURIComponent(BACKEND_URL)}`);

  // Connected: the status pill in the header shows the backend's engine (from its hello), not "connecting".
  // innerText: the pill also holds its other labels, hidden, to keep its width.
  const pill = page.getByRole("banner").getByRole("status");
  await expect(pill).toHaveText("cpp-sdk-backend", { useInnerText: true, timeout: 20_000 });

  // The starter graph from backend.toml is on the Flow page.
  await expect(page.getByText("2 blocks", { exact: true })).toBeVisible();

  // Run it as fast as it goes (the backend's default is real time for 10 s), and wait for the end.
  const dock = page.getByRole("group", { name: "Run" });
  await dock.getByRole("combobox").selectOption({ label: "max" });
  const run = dock.getByRole("button", { name: "Run" });
  await expect(run).toBeEnabled();
  await run.click();
  await expect(dock.getByText("done", { exact: true })).toBeVisible({ timeout: 30_000 });

  // The Graph block's instrument on the Board got the samples: a trace, and the latest value, 2.
  await page.getByRole("button", { name: "board", exact: true }).click();
  const graph = page.getByRole("figure").filter({ hasText: "graph" });
  await expect(graph).toBeVisible();
  await expect(graph.locator("polyline.trace")).toHaveCount(1);
  await expect(graph.locator("output")).toHaveText(/^2(\.0+)?$/);

  // The Console has the backend's log line for the run.
  await page.getByRole("button", { name: "Console", exact: true }).click();
  await expect(page.getByRole("dialog", { name: "Console" })).toContainText("running 2 blocks");
});
