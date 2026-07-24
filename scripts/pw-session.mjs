import { chromium } from 'playwright';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const extensionPath = path.join(root, 'chatterino-extension');
const userDataDir = path.join(root, '.playwright-companion-profile');
const url = process.env.TWITCH_URL || 'https://www.twitch.tv/ohnePixel';

// Set CHATTERINO_EXT to the real Chatterino Native Host extension's src dir
// to test both extensions together.
const extensions = [extensionPath];
if (process.env.CHATTERINO_EXT) {
  extensions.push(path.resolve(process.env.CHATTERINO_EXT));
}

const context = await chromium.launchPersistentContext(userDataDir, {
  headless: false,
  viewport: { width: 1600, height: 900 },
  args: [
    `--disable-extensions-except=${extensions.join(',')}`,
    `--load-extension=${extensions.join(',')}`,
    '--remote-debugging-port=9223',
  ],
});

const page = context.pages()[0] || (await context.newPage());
await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 90000 });
console.log('SESSION_READY', url);

await new Promise(() => {});
