import { chromium } from 'playwright';
import fs from 'node:fs';

const file = process.argv[2];
if (!file) {
  console.error('usage: node pw-eval.mjs <script.mjs-snippet>');
  process.exit(1);
}
const code = fs.readFileSync(file, 'utf8');

const browser = await chromium.connectOverCDP('http://127.0.0.1:9223');
const context = browser.contexts()[0];
const page = context.pages().find((p) => p.url().includes('twitch.tv')) || context.pages()[0];

const fn = new Function('page', 'context', 'browser', `return (async () => { ${code} })();`);
try {
  await fn(page, context, browser);
} catch (e) {
  console.error('EVAL_ERROR', e);
  process.exitCode = 1;
}
await browser.close();
