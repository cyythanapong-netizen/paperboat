import { open, place } from "./lib.mjs";
const { browser, page } = await open();
const A = "Modelica.Electrical.Analog", R = "Modelica.Mechanics.Rotational";
for (const c of [`${A}.Sources.SignalVoltage`, `${A}.Basic.Resistor`, `${A}.Basic.Inductor`, `${A}.Basic.RotationalEMF`, `${A}.Basic.Ground`,
  `${R}.Components.Inertia`, `${R}.Components.SpringDamper`, `${R}.Components.Inertia`, `${R}.Sensors.SpeedSensor`, `${R}.Sensors.AngleSensor`]) await place(page, c);
console.log(await page.locator(".react-flow__node").evaluateAll(n => n.map(x => x.dataset.id)));
await page.screenshot({ path: "/tmp/placed.png" });
await browser.close();
