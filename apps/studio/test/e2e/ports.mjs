// Puts the drive's input on the left of the plate and saves the Look.
import { open } from "./lib.mjs";
const { browser, page } = await open();
await page.waitForTimeout(2000);
await page.keyboard.press("Alt+v");
const sheet = page.getByLabel("Interface and Look");
await sheet.getByText("look", { exact: true }).first().click();
await sheet.getByLabel("Port placement").locator(".studio-look-port-row").filter({ hasText: /^v/ }).getByText("left", { exact: true }).click();
await sheet.getByRole("button", { name: "Save" }).click();
await page.waitForTimeout(1500);
await browser.close();
