// Draws the DC drive's motor-and-load plate in the Look tab (2x1) and saves it.
import { open } from "./lib.mjs";
const { browser, page } = await open();
await page.waitForTimeout(2000);
await page.keyboard.press("Alt+v");
const sheet = page.getByLabel("Interface and Look");
await sheet.getByText("look", { exact: true }).first().click();
const svg = sheet.getByLabel("Look 0 to 100 icon canvas");
await svg.waitFor();
const tokens = sheet.getByLabel("Style tokens");
await tokens.getByText("2x1", { exact: true }).click();
const at = async (x, y) => { const b = await svg.boundingBox(); return [b.x + (x * b.width) / 100, b.y + (y * b.height) / 100]; };
async function drag(from, dx) {
  const [x0, y0] = await at(...from);
  const [x1] = await at(from[0] + dx, from[1]);
  await page.mouse.move(x0, y0);
  await page.mouse.down();
  await page.mouse.move((x0 + x1) / 2, y0, { steps: 4 });
  await page.mouse.move(x1, y0, { steps: 4 });
  await page.mouse.up();
}
const add = (k) => sheet.getByRole("button", { name: `+ ${k}` }).click();
await add("ellipse"); await tokens.getByText("raised", { exact: true }).click(); await drag([70, 50], -25);   // motor body
await add("line");    await tokens.getByText("wire", { exact: true }).click();   await drag([50, 50], 25);    // shaft
await add("ellipse"); await drag([70, 50], 30);                                                              // load disc
await add("text");    await drag([50, 50], -25);                                                             // M
await page.screenshot({ path: "/tmp/look.png" });
await sheet.getByRole("button", { name: "Save" }).click();
await page.waitForTimeout(1500);
console.log(await sheet.locator(".studio-look-foot").innerText());
await browser.close();
