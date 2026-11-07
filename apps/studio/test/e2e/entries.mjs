import { open } from "./lib.mjs";
const { browser, page } = await open();
await page.waitForTimeout(2000);
const dlg = (n) => page.locator(`[aria-label="${n} parameters"]`);
async function expose(part, port) {
  await page.locator(`.react-flow__node[data-id="${part}"]`).click({ position: { x: 20, y: 20 } });
  await dlg(part).waitFor();
  console.log(part, "popover:", (await dlg(part).innerText()).replace(/\n/g, " | ").slice(0, 200));
  await dlg(part).locator(".studio-port-row").filter({ has: page.locator(".studio-port-name", { hasText: new RegExp(`^${port}$`) }) }).getByRole("button").click();
  await page.waitForTimeout(1200);
}
await expose("V1", "v");
await expose("S2", "w");
await expose("A1", "phi");
await page.screenshot({ path: "/tmp/entries.png" });
await browser.close();
