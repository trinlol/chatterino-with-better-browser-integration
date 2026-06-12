import { chromium } from 'playwright';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const extensionPath = path.join(root, 'chatterino-companion');
const userDataDir = path.join(root, '.playwright-companion-profile');
const url = process.env.TWITCH_URL || 'https://www.twitch.tv/ohnePixel';

const context = await chromium.launchPersistentContext(userDataDir, {
  headless: false,
  viewport: { width: 1600, height: 900 },
  args: [
    `--disable-extensions-except=${extensionPath}`,
    `--load-extension=${extensionPath}`,
  ],
});

const page = context.pages()[0] || (await context.newPage());
await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 90000 });
console.log('Browser open at', url);
console.log('Extension loaded from', extensionPath);
console.log('Profile:', userDataDir);
console.log('Close this terminal or press Ctrl+C when done.');

await page.waitForTimeout(3000);
const accept = page.getByRole('button', { name: /^Accept$/i });
if (await accept.isVisible().catch(() => false)) {
  await accept.click().catch(() => {});
}

for (let i = 0; i < 12; i++) {
  await page.waitForTimeout(2000);
  const state = await page.evaluate(() => {
    const rect = (sel) => {
      const el = document.querySelector(sel);
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height) };
    };
    const summary = document.querySelector('[data-test-selector="community-points-summary"]');
    return {
      follow: rect('[data-a-target="follow-button"], [data-a-target="unfollow-button"]'),
      portal: rect('#chatterino-toolbar-portal'),
      slot: rect('#chatterino-toolbar-slot'),
      toolbarPoints: rect('#chatterino-toolbar-slot [data-test-selector="community-points-summary"]'),
      holderPoints: rect('#chatterino-native-points-holder [data-test-selector="community-points-summary"]'),
      summaryParent: summary?.parentElement?.id || summary?.parentElement?.className?.slice(0, 40) || null,
      slotChildCount: document.getElementById('chatterino-toolbar-slot')?.childElementCount ?? 0,
    };
  });
  console.log(`[${i + 1}]`, JSON.stringify(state));
}

await new Promise(() => {});
