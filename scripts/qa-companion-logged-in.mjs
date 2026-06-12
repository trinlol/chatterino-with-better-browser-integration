import { chromium } from 'playwright';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const extensionPath = path.join(root, 'chatterino-companion');
const userDataDir = path.join(root, '.playwright-companion-profile');
const outDir = path.join(root, 'scripts', 'qa-screenshots');
const url = process.env.TWITCH_URL || 'https://www.twitch.tv/ohnePixel';

fs.mkdirSync(outDir, { recursive: true });

const context = await chromium.launchPersistentContext(userDataDir, {
  headless: false,
  viewport: { width: 1600, height: 900 },
  args: [
    `--disable-extensions-except=${extensionPath}`,
    `--load-extension=${extensionPath}`,
  ],
});

const page = context.pages()[0] || (await context.newPage());

async function shot(name, clipLocator) {
  const file = path.join(outDir, `${name}.jpg`);
  if (clipLocator) {
    const box = await clipLocator.boundingBox().catch(() => null);
    if (box) {
      await page.screenshot({
        path: file,
        type: 'jpeg',
        quality: 90,
        clip: {
          x: Math.max(0, box.x - 24),
          y: Math.max(0, box.y - 12),
          width: Math.min(1600, box.width + 160),
          height: Math.min(900, box.height + 24),
        },
      });
      console.log('screenshot:', file);
      return;
    }
  }
  await page.screenshot({ path: file, type: 'jpeg', quality: 85 });
  console.log('screenshot:', file);
}

async function readState() {
  return page.evaluate(() => {
    const rect = (sel) => {
      const el = document.querySelector(sel);
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height) };
    };
    const follow = document.querySelector('[data-a-target="follow-button"], [data-a-target="unfollow-button"]');
    const toolbarPts = document.querySelector('#chatterino-toolbar-slot [data-test-selector="community-points-summary"]');
    const fRect = follow?.getBoundingClientRect();
    const pRect = toolbarPts?.getBoundingClientRect();
    let gap = null;
    if (fRect && pRect && pRect.width > 0) {
      gap = Math.round(fRect.left - (pRect.left + pRect.width));
    }
    return {
      toolbarMounted: !!document.getElementById('chatterino-toolbar-portal'),
      nativeInToolbar: !!toolbarPts,
      nativeInHolder: !!document.querySelector('#chatterino-native-points-holder [data-test-selector="community-points-summary"]'),
      follow: rect('[data-a-target="follow-button"], [data-a-target="unfollow-button"]'),
      toolbarPoints: rect('#chatterino-toolbar-slot [data-test-selector="community-points-summary"]'),
      gapPx: gap,
      pointsLabel: toolbarPts?.innerText?.replace(/\s+/g, ' ').trim().slice(0, 40) || null,
      rewardDialog: !!document.querySelector('div[role="dialog"] .reward-center__content, div[role="dialog"]:has(.reward-center__content)'),
      companionActive: document.documentElement.classList.contains('chatterino-companion-active'),
    };
  });
}

try {
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 90000 });
  const accept = page.getByRole('button', { name: /^Accept$/i });
  if (await accept.isVisible().catch(() => false)) await accept.click().catch(() => {});

  await page.evaluate(() =>
    document.querySelector('[data-a-target="follow-button"], [data-a-target="unfollow-button"]')?.scrollIntoView({ block: 'center' })
  );
  await page.waitForTimeout(8000);

  let state = await readState();
  console.log('Initial:', JSON.stringify(state, null, 2));
  await shot('07-native-toolbar-fixed', page.locator('#chatterino-toolbar-slot'));

  const nativeBtn = page.locator('#chatterino-toolbar-slot [data-test-selector="community-points-summary"] button');
  if (await nativeBtn.isVisible().catch(() => false)) {
    await nativeBtn.click({ timeout: 5000 }).catch(() =>
      page.evaluate(() =>
        document.querySelector('#chatterino-toolbar-slot [data-test-selector="community-points-summary"] button')?.click()
      )
    );
    await page.waitForTimeout(2000);
    state = await readState();
    console.log('After click:', JSON.stringify(state, null, 2));
    await shot('08-native-reward-menu', page.locator('div[role="dialog"]').first());
  }

  // Simulate Chatterino wipe
  await page.evaluate(() => {
    const shell = document.querySelector('.chat-shell');
    if (shell?.children[0]) {
      shell.children[0].innerHTML =
        '<div>Chatterino should show here. Make sure Chatterino is running.</div>';
    }
  });
  await page.waitForTimeout(3000);
  state = await readState();
  console.log('Post-wipe:', JSON.stringify(state, null, 2));
  await shot('09-post-wipe-toolbar', page.locator('#chatterino-toolbar-slot'));

  console.log('\n=== RESULT ===');
  console.log('Native in toolbar:', state.nativeInToolbar);
  console.log('Gap before Follow (px):', state.gapPx);
  console.log('Points label:', state.pointsLabel);
  console.log('Reward dialog opened:', state.rewardDialog);
} finally {
  await context.close();
}
