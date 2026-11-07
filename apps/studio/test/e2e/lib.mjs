// Shared helpers for driving Squero Studio OM headlessly (needs playwright-core + chromium).
export const PW = process.env.PW_CORE ?? "/tmp/claude-1000/pw/node_modules/playwright-core/index.mjs";
export const CHROME = process.env.CHROME ?? `${process.env.HOME}/.cache/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-linux64/chrome-headless-shell`;
export const { chromium } = await import(PW);
export async function open(url = "http://localhost:8790/") {
  const browser = await chromium.launch({ executablePath: CHROME });
  const page = await browser.newPage({ viewport: { width: 1500, height: 900 } });
  page.on("console", (m) => m.type() === "error" && console.log("console:", m.text()));
  await page.goto(url);
  await page.waitForSelector(".library");
  return { browser, page };
}
/** Filter the library to one class and double-click it onto the canvas. */
export async function place(page, cls) {
  const filter = page.getByLabel("Filter parts");
  await filter.fill(cls);
  await page.waitForFunction((c) => [...document.querySelectorAll(".lib-part")].some((r) => r.title.split("\n")[0] === c), cls);
  const rows = page.locator(".lib-part");
  const at = await rows.evaluateAll((rs, c) => rs.findIndex((r) => r.title.split("\n")[0] === c), cls);
  const n = await page.locator(".react-flow__node").count();
  await rows.nth(at).dblclick();
  await page.waitForFunction((k) => document.querySelectorAll(".react-flow__node").length > k, n);
  await page.waitForSelector(".chip.omc-ready", { timeout: 5000 }).catch(() => {});
  await page.waitForTimeout(500);
}
/** Drag a wire from one port to another: "V1.p" to "R1.p". */
export async function wire(page, a, b) {
  const h = async (path, kind) => {
    const [part, port] = path.split(".");
    const box = await page.locator(`.react-flow__node[data-id="${part}"] .react-flow__handle[data-handleid="${port}"].${kind}`).first().boundingBox();
    return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
  };
  const from = await h(a, "source");
  const to = await h(b, "target");
  const k = await page.locator(".react-flow__edge").count();
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move((from.x + to.x) / 2, (from.y + to.y) / 2, { steps: 5 });
  await page.mouse.move(to.x, to.y, { steps: 5 });
  await page.mouse.up();
  await page.waitForFunction((n) => document.querySelectorAll(".react-flow__edge").length > n, k, { timeout: 5000 }).catch(() => console.log("no wire", a, b));
  await page.waitForTimeout(300);
}
