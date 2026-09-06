// Attach to running Edge via CDP; edge://extensions is presumed open.
import { chromium } from "playwright";
import { writeFileSync } from "node:fs";

const port = process.argv[2] || "9333";
const out = process.argv[3] || (process.env.TEMP + "\\chatterino-cdp-dump.json");

const browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
const result = {};

let page = null;
for (const ctx of browser.contexts()) {
  for (const p of ctx.pages()) {
    if (p.url().startsWith("edge://extensions")) { page = p; break; }
  }
  if (page) break;
}
if (!page) {
  result.noPage = true;
  for (const ctx of browser.contexts()) {
    for (const p of ctx.pages()) result.pages = [...(result.pages || []), p.url()];
  }
} else {
  await page.waitForTimeout(8000);
  result.url = page.url();

  // Try several signatures of developerPrivate.getExtensionsInfo
  const raw = await page.evaluate(async () => {
    const dev = window.chrome?.developerPrivate;
    if (!dev) return { noDev: true };
    const attempts = [];
    for (const args of [
      [{ includeDisabled: true, includeTerminated: true }],
      [{}],
      [],
    ]) {
      try {
        const res = await new Promise((resolve, reject) => {
          try {
            dev.getExtensionsInfo(...args, (exts) => resolve(exts));
          } catch (e) { reject(e); }
        });
        attempts.push({ args, ok: true, count: res?.length });
        if (res?.length) {
          const mapped = res.map((e) => ({
            id: e.id, name: e.name, state: e.state, location: e.location,
            disableReasons: e.disableReasons, path: e.path, version: e.version,
            errors: e.manifestErrors,
          })).filter((e) => /chatterino|bogfpdfo|oenpbjp|boieha|twitch|predictions/i.test(e.id + e.name + (e.path || "")));
          attempts[attempts.length - 1].matches = mapped;
        }
      } catch (e) {
        attempts.push({ args, ok: false, err: String(e) });
      }
    }
    const all = await new Promise((resolve) => {
      try {
        dev.getExtensionsInfo({ includeDisabled: true, includeTerminated: true }, (exts) =>
          resolve(exts.map((e) => ({ id: e.id, name: e.name, state: e.state, location: e.location, disableReasons: e.disableReasons, path: e.path }))));
      } catch (e) { resolve({ err: String(e) }); }
    });
    return { attempts, all };
  });
  result.developerPrivate = raw;

  result.bodyText = await page.evaluate(() => document.body?.innerText || "").catch(() => "");
  result.bodySnippet = result.bodyText.slice(0, 8000);
}

writeFileSync(out, JSON.stringify(result, null, 1));
console.log(JSON.stringify({ wrote: out, pageUrl: result.url || null, noPage: result.noPage || false }));
await browser.close();