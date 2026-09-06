// Launch the REAL profile via Playwright and introspect the extensions page.
import { chromium } from "playwright";
import { writeFileSync } from "node:fs";

const userData = process.env.LOCALAPPDATA + "\\Microsoft\\Edge\\User Data";
const edge = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const out = process.env.TEMP + "\\chatterino-real-playwright-dump.json";

const ctx = await chromium.launchPersistentContext(userData, {
  executablePath: edge,
  headless: false,
  viewport: { width: 1100, height: 800 },
  ignoreDefaultArgs: [
    "--disable-extensions",
    "--disable-component-extensions-with-background-pages",
    "--disable-default-apps",
  ],
  args: ["--no-first-run", "--no-default-browser-check"],
});

const page = await ctx.newPage();
await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 }).catch((e) => console.log("goto err", e.message));
await page.waitForTimeout(10000);

const result = { url: page.url() };

// Visible text
result.bodyText = await page.evaluate(() => document.body?.innerText || "").catch(() => "");

// developerPrivate introspection
result.dev = await page.evaluate(async () => {
  const dev = window.chrome?.developerPrivate;
  if (!dev) return { noDev: true, chromeKeys: Object.keys(window.chrome || {}) };
  const out = {};
  for (const [label, args] of [
    ["opts", [{ includeDisabled: true, includeTerminated: true }]],
    ["empty", [{}]],
    ["none", []],
  ]) {
    try {
      const exts = await new Promise((resolve, reject) => {
        try { dev.getExtensionsInfo(...args, (r) => resolve(r)); } catch (e) { reject(e); }
      });
      out[label] = exts.map((e) => ({
        id: e.id, name: e.name, state: e.state, location: e.location,
        disableReasons: e.disableReasons, path: e.path, version: e.version,
        manifestErrors: e.manifestErrors,
      }));
    } catch (e) { out[label] = { err: String(e) }; }
  }
  return out;
});

writeFileSync(out, JSON.stringify(result, null, 1));
console.log(JSON.stringify({ wrote: out, url: result.url, bodyLen: result.bodyText?.length, devKeys: Object.keys(result.dev || {}) }));
await ctx.close();