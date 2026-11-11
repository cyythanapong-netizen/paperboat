import { chromium } from "../../../node_modules/playwright/index.mjs";

const base = process.env.BASE || "http://127.0.0.1:8801";
const here = new URL(".", import.meta.url).pathname;
const files = ["a-bench", "b-split", "c-board", "d-start", "e-editor", "f-variables"];
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1.5 });
page.on("pageerror", (e) => console.log("pageerror:", e.message));
for (const f of files) {
  await page.goto(`${base}/${f}.html`);
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(600);
  await page.screenshot({ path: `${here}${f}.jpg`, type: "jpeg", quality: 90 });
  console.log("shot", f);
}
await browser.close();
