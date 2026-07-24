import { chromium } from "playwright";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const profile = path.join(root, ".playwright-companion-profile");
const ext = path.join(root, "chatterino-extension");

const ctx = await chromium.launchPersistentContext(profile, {
  headless: true,
  viewport: { width: 1600, height: 900 },
  args: [`--disable-extensions-except=${ext}`, `--load-extension=${ext}`],
});
const page = ctx.pages()[0] || (await ctx.newPage());
await page.goto("https://www.twitch.tv/ohnePixel", {
  waitUntil: "domcontentloaded",
  timeout: 60000,
});
await page.waitForTimeout(6000);
await page.evaluate(() =>
  document
    .querySelector(
      '[data-a-target="follow-button"], [data-a-target="unfollow-button"]'
    )
    ?.scrollIntoView({ block: "center" })
);
await page.waitForTimeout(1000);
const box = await page.locator("#chatterino-toolbar-slot").boundingBox();
if (box) {
  await page.screenshot({
    path: path.join(root, "scripts/qa-screenshots/07-native-toolbar-fixed.jpg"),
    type: "jpeg",
    quality: 90,
    clip: {
      x: Math.max(0, box.x - 20),
      y: Math.max(0, box.y - 15),
      width: box.width + 140,
      height: box.height + 30,
    },
  });
}
await ctx.close();
console.log("done");
