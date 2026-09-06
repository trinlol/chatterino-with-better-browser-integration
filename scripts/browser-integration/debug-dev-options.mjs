// Probe the Developer options section in Edge's extensions page.
import { chromium } from "playwright";
import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const profile = await mkdtemp(path.join(os.tmpdir(), "chatterino-debug-"));
const ctx = await chromium.launchPersistentContext(profile, {
  executablePath: "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  headless: false,
  viewport: { width: 1100, height: 800 },
  ignoreDefaultArgs: ["--disable-extensions", "--disable-component-extensions-with-background-pages"],
  args: ["--no-first-run", "--no-default-browser-check"],
});
const page = await ctx.newPage();
await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
await page.waitForSelector("root-app", { timeout: 20000 });
const sw = page.locator("developer-mode-switch").first();
await sw.waitFor({ state: "attached", timeout: 20000 });
await sw.evaluate((el) => el.shadowRoot.querySelector("fluent-switch").click());
await page.waitForTimeout(1000);

const probe = await page.evaluate(() => {
  const lines = [];
  const seen = new Set();
  function walk(node, depth) {
    if (!node || depth > 14 || seen.has(node)) return;
    seen.add(node);
    if (node.nodeType === 11) { for (const c of node.children) walk(c, depth); return; }
    if (!node.tagName) return;
    const t = (node.textContent || "").trim();
    const id = node.id ? `#${node.id}` : "";
    const cls = typeof node.className === "string" && node.className ? `.${node.className.split(/\s+/).join(".")}` : "";
    const box = node.getBoundingClientRect ? (() => { const r = node.getBoundingClientRect(); return r.width > 0 && r.height > 0 ? `[${Math.round(r.width)}x${Math.round(r.height)}]` : "[0x0]"; })() : "";
    if (/developer options|load unpacked|pack extension/i.test(t) && t.length < 80) {
      lines.push(`${"  ".repeat(depth)}<${node.tagName.toLowerCase()}${id}${cls} ${box}> "${t.slice(0, 60)}"`);
    }
    if (node.shadowRoot) walk(node.shadowRoot, depth + 1);
    for (const c of node.children) walk(c, depth + 1);
  }
  walk(document.body, 0);
  return lines.join("\n");
});
console.log(probe);
await ctx.close();