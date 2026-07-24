import { chromium } from 'playwright';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const extensionPath = path.join(root, 'chatterino-extension');
const userDataDir = path.join(root, '.playwright-companion-profile');
const outDir = path.join(root, 'scripts', 'qa-screenshots');

fs.mkdirSync(outDir, { recursive: true });

const TARGET_URL = process.env.TWITCH_URL || 'https://www.twitch.tv/xqc';
const VIEWPORT = { width: 1600, height: 900 };

const CHATTERINO_WIPE_HTML = `
<div style="padding:16px;text-align:center;color:#adadb8;">
  Chatterino should show here. Make sure Chatterino is running and the browser extension is enabled.
</div>`;

async function shot(page, name) {
  const file = path.join(outDir, `${name}.jpg`);
  await page.screenshot({ path: file, type: 'jpeg', quality: 85, fullPage: false });
  console.log('screenshot:', file);
  return file;
}

async function dismissCookieBanner(page) {
  const accept = page.getByRole('button', { name: /^Accept$/i });
  if (await accept.isVisible().catch(() => false)) {
    await accept.click({ timeout: 3000 }).catch(() => {});
    await page.waitForTimeout(500);
  }
}

async function scrollFollowIntoView(page) {
  await page.evaluate(() => {
    const btn =
      document.querySelector('[data-a-target="follow-button"]') ||
      document.querySelector('[data-a-target="unfollow-button"]');
    btn?.scrollIntoView({ block: 'center', inline: 'nearest' });
  });
  await page.waitForTimeout(500);
}

async function clickNativePointsInToolbar(page) {
  return page.evaluate(() => {
    const btn = document.querySelector(
      '#chatterino-toolbar-slot [data-test-selector="community-points-summary"] button'
    );
    if (!btn) return { ok: false, reason: 'no native toolbar button' };
    btn.click();
    return { ok: true, label: btn.getAttribute('aria-label') };
  });
}

async function readState(page) {
  return page.evaluate(() => {
    const rect = (sel) => {
      const el = document.querySelector(sel);
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: r.x, y: r.y, w: r.width, h: r.height, visible: r.width > 0 && r.height > 0 };
    };
    return {
      companionActive: document.documentElement.classList.contains('chatterino-companion-active'),
      toolbarMounted: !!document.getElementById('chatterino-toolbar-portal'),
      toolbarSlot: rect('#chatterino-toolbar-slot'),
      pointsReplica: !!document.querySelector('#chatterino-toolbar-slot [data-test-selector="community-points-summary"]'),
      pointsReplica: rect('#chatterino-points-replica'),
      predictionReplica: rect('#chatterino-prediction-replica'),
      pollReplica: rect('#chatterino-poll-replica'),
      domPoints: !!document.querySelector('[data-test-selector="community-points-summary"]'),
      follow: rect('[data-a-target="follow-button"], [data-a-target="unfollow-button"]'),
      chatWiped: (() => {
        const shell = document.querySelector('.chat-shell');
        const text = shell?.children[0]?.innerText || '';
        return text.includes('Chatterino should show here');
      })(),
      gqlBalance: document.documentElement.getAttribute('data-cc-gql-balance'),
      rewardDialog: !!document.querySelector('div[role="dialog"] .reward-center__content'),
      viewport: { w: window.innerWidth, h: window.innerHeight },
    };
  });
}

async function simulateChatterinoWipe(page) {
  return page.evaluate((html) => {
    const shell = document.querySelector('.chat-shell');
    if (!shell?.children[0]) return { ok: false, reason: 'no chat-shell child' };
    shell.children[0].innerHTML = html;
    return { ok: true };
  }, CHATTERINO_WIPE_HTML);
}

async function waitForToolbar(page, timeoutMs = 45000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const state = await readState(page);
    if (state.toolbarMounted) return state;
    await page.waitForTimeout(1000);
  }
  return readState(page);
}

console.log('Extension path:', extensionPath);
console.log('Target:', TARGET_URL);

const context = await chromium.launchPersistentContext(userDataDir, {
  headless: false,
  viewport: VIEWPORT,
  args: [
    `--disable-extensions-except=${extensionPath}`,
    `--load-extension=${extensionPath}`,
    '--disable-blink-features=AutomationControlled',
  ],
});

const page = context.pages()[0] || (await context.newPage());

try {
  await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 90000 });
  console.log('Title:', await page.title());
  await dismissCookieBanner(page);
  await page.waitForTimeout(6000);
  await scrollFollowIntoView(page);

  let state = await waitForToolbar(page);
  console.log('Initial state:', JSON.stringify(state, null, 2));
  await shot(page, '01-initial');

  // Capture toolbar region if present
  const toolbarBox = await page.locator('#chatterino-toolbar-portal').boundingBox().catch(() => null);
  if (toolbarBox) {
    await page.screenshot({
      path: path.join(outDir, '02-toolbar-clip.jpg'),
      type: 'jpeg',
      quality: 90,
      clip: {
        x: Math.max(0, toolbarBox.x - 20),
        y: Math.max(0, toolbarBox.y - 10),
        width: Math.min(VIEWPORT.width, toolbarBox.width + 400),
        height: Math.min(VIEWPORT.height, toolbarBox.height + 40),
      },
    });
    console.log('screenshot: toolbar clip');
  }

  const pointsBtn = page.locator('#chatterino-toolbar-slot [data-test-selector="community-points-summary"] button');
  const pointsVisible = await pointsBtn.isVisible().catch(() => false);
  console.log('Native points in toolbar visible:', pointsVisible);

  if (pointsVisible) {
    const clickResult = await clickNativePointsInToolbar(page);
    console.log('Native toolbar click:', clickResult);
    await page.waitForTimeout(2000);
    state = await readState(page);
    console.log('After points click:', JSON.stringify(state, null, 2));
    await shot(page, '03-after-points-click');

    const dialog = page.locator('div[role="dialog"] .reward-center__content, div[role="dialog"]:has-text("Power-ups")');
    const dialogVisible = await dialog.first().isVisible().catch(() => false);
    console.log('Native reward dialog visible:', dialogVisible);
    if (dialogVisible) {
      await shot(page, '04-native-reward-dialog');
    }
  }

  // Simulate official Chatterino chat wipe (post-capture stress test)
  console.log('Simulating Chatterino chat wipe...');
  const wipeResult = await simulateChatterinoWipe(page);
  console.log('Wipe result:', wipeResult);
  await page.waitForTimeout(3000);
  await scrollFollowIntoView(page);

  state = await readState(page);
  console.log('Post-wipe state:', JSON.stringify(state, null, 2));
  await shot(page, '05-post-wipe');

  if (await pointsBtn.isVisible().catch(() => false)) {
    await clickNativePointsInToolbar(page);
    await page.waitForTimeout(2000);
    state = await readState(page);
    console.log('Post-wipe points click:', JSON.stringify(state, null, 2));
    await shot(page, '06-post-wipe-menu');

    const dialogAfterWipe = page.locator('div[role="dialog"] .reward-center__content, div[role="dialog"]:has-text("Power-ups")');
    console.log('Dialog after wipe click:', await dialogAfterWipe.first().isVisible().catch(() => false));
  }

  // Viewport fit for toolbar
  if (state.toolbarSlot) {
    const { x, y, w, h } = state.toolbarSlot;
    const inViewport = x >= 0 && y >= 0 && x + w <= state.viewport.w && y + h <= state.viewport.h;
    console.log('Toolbar in viewport:', inViewport, state.toolbarSlot);
  }

  console.log('\n=== QA SUMMARY ===');
  console.log('Toolbar mounted:', state.toolbarMounted);
  console.log('Companion active:', state.companionActive);
  console.log('Points replica visible:', !!state.pointsReplica?.visible);
  console.log('Prediction replica visible:', !!state.predictionReplica?.visible);
  console.log('Poll replica visible:', !!state.pollReplica?.visible);
  console.log('GQL balance attr:', state.gqlBalance);
  console.log('Chat wiped:', state.chatWiped);
  console.log('Screenshots in:', outDir);
} finally {
  await context.close();
}
