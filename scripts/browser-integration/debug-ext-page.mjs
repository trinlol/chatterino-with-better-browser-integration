// Probe developer-mode-switch structure via Playwright locator (pierces shadow).
import { chromium } from "playwright";
import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const profile = await mkdtemp(path.join(os.tmpdir(), "chatterino-debug-"));
const ctx = await chromium.launchPersistentContext(profile, {
  executablePath: "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  headless: false,
  viewport: { width: 1100, height: 800 },
  args: ["--no-first-run", "--no-default-browser-check"],
});
const page = await ctx.newPage();
await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
await page.waitForSelector("root-app", { timeout: 20000 });
await page.waitForTimeout(3000);

const sw = page.locator("developer-mode-switch");
const n = await sw.count();
console.log("count:", n);
for (let i = 0; i < n; i++) {
  const info = await sw.nth(i).evaluate((el) => {
    const sr = el.shadowRoot;
    return JSON.stringify({
      outer: el.outerHTML.slice(0, 200),
      shadowOpen: !!sr,
      shadowInner: sr ? sr.innerHTML.slice(0, 1500) : null,
      lightChildren: [...el.children].map((c) => c.tagName.toLowerCase()),
    }, null, 1);
  });
  console.log(`switch[${i}]`, info);
}

// Also find the fluent-switch / any input anywhere near.
const allInputs = await page.locator("input").count();
console.log("total inputs on page:", allInputs);
await ctx.close();