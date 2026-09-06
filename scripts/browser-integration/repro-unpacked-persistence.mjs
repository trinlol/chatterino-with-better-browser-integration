// Investigation harness: does an unpacked extension loaded via the
// edge://extensions "Load unpacked" UI survive a clean browser restart in
// Microsoft Edge 152?
//
// Phase A: fresh temp profile -> drive the real Load unpacked UI -> verify the
//          extension card appears.
// Phase B: close fully -> relaunch the SAME profile WITHOUT any --load-extension
//          flag -> check whether the extension card is still listed.
//
// This mirrors the user's report ("extension keeps removing itself when we close
// the browser") in an isolated profile so the investigation does not disturb the
// live profile.
//
// Usage: node scripts/browser-integration/repro-unpacked-persistence.mjs
//        [--edge <path-to-msedge.exe>] [--extension <dir>] [--profile <dir>]
import { chromium } from "playwright";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const EDGE_DEFAULT = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const EXT_DEFAULT = path.resolve("chatterino-extension");

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}
const edgePath = arg("--edge", EDGE_DEFAULT);
const extDir = path.resolve(arg("--extension", EXT_DEFAULT));
const profileArg = arg("--profile", "");

const profile = profileArg || await mkdtemp(path.join(os.tmpdir(), "chatterino-repro-"));
console.log(JSON.stringify({ phase: "setup", edgePath, extDir, profile }));

// ---- shadow-piercing helpers for Edge's Web Components page ----
async function enableDevMode(page) {
  await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
  await page.waitForSelector("root-app", { timeout: 20000 });
  const sw = page.locator("developer-mode-switch").first();
  await sw.waitFor({ state: "attached", timeout: 20000 });
  // The switch sits in a collapsed nav pane (no bounding box), so drive a
  // native DOM click instead of a Playwright action.
  await sw.evaluate((el) => {
    const fs = el.shadowRoot.querySelector("fluent-switch");
    if (!fs) throw new Error("no fluent-switch inside developer-mode-switch");
    fs.click();
  });
  await page.waitForTimeout(800);
  // Confirm dev mode actually turned on (checked attribute flips).
  const checked = await sw.evaluate((el) =>
    el.shadowRoot?.querySelector("fluent-switch")?.getAttribute("checked"));
  if (checked !== "true") throw new Error("developer mode did not enable");
}

// Collect every text node across all open shadow roots.
function collectTexts() {
  const parts = [];
  const seen = new Set();
  function walk(node) {
    if (!node || seen.has(node)) return;
    seen.add(node);
    if (node.nodeType === Node.TEXT_NODE) {
      const t = node.textContent.trim();
      if (t) parts.push(t);
      return;
    }
    if (node.shadowRoot) walk(node.shadowRoot);
    for (const c of node.childNodes) walk(c);
  }
  walk(document.body);
  return parts;
}

function findByText(needle) {
  const els = [];
  const seen = new Set();
  function walk(node) {
    if (!node || seen.has(node)) return;
    seen.add(node);
    for (const el of node.querySelectorAll ? node.querySelectorAll("*") : []) {
      const t = (el.textContent || "").trim();
      if (t === needle && els.length < 3) els.push(el);
    }
    if (node.shadowRoot) walk(node.shadowRoot);
  }
  walk(document.body);
  return els;
}

async function loadUnpacked(page) {
  const [chooser] = await Promise.all([
    page.waitForEvent("filechooser", { timeout: 20000 }),
    page.getByText("Load unpacked", { exact: true }).first().click(),
  ]);
  await chooser.setFiles(extDir);
  await page.waitForTimeout(2500);
}

async function chatterinoCardNames(page) {
  return page.evaluate(() => {
    const texts = collectTexts();
    return JSON.stringify(texts.filter((t) => /chatterino/i.test(t)).slice(0, 20));
  });
}

async function launch(profileDir) {
  return chromium.launchPersistentContext(profileDir, {
    executablePath: edgePath,
    headless: false,
    viewport: { width: 1100, height: 800 },
    args: ["--no-first-run", "--no-default-browser-check"],
  });
}

async function readPrefs(profileDir, name) {
  const f = path.join(profileDir, name);
  try {
    return JSON.parse(await readFile(f, "utf8"));
  } catch {
    return null;
  }
}

const result = { profile, edgePath, extDir, phases: {} };

// ---- Phase A: load unpacked ----
{
  const ctx = await launch(profile);
  const page = await ctx.newPage();
  page.on("pageerror", (e) => console.log("[pageerror]", e.message));
  await enableDevMode(page);
  await loadUnpacked(page);
  const hitsA = JSON.parse(await chatterinoCardNames(page));
  result.phases.A_afterLoad = { chatterinoTextHits: hitsA };
  console.log(JSON.stringify({ phase: "A_afterLoad", hitsA }));
  await ctx.close();
}

// ---- Disable startup boost so Phase B is a clean launch ----
{
  const lsPath = path.join(profile, "Local State");
  try {
    const ls = JSON.parse(await readFile(lsPath, "utf8"));
    if (!ls.startup_boost) ls.startup_boost = {};
    ls.startup_boost.enabled = false;
    await writeFile(lsPath, JSON.stringify(ls));
    result.phases.startupBoostDisabled = true;
  } catch (e) {
    result.phases.startupBoostDisabled = `could not edit: ${e.message}`;
  }
}

// ---- Phase B: clean relaunch, no --load-extension ----
{
  const ctx = await launch(profile);
  const page = await ctx.newPage();
  await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
  await page.waitForSelector("root-app", { timeout: 20000 });
  await page.waitForTimeout(2500);
  const hitsB = JSON.parse(await chatterinoCardNames(page));
  result.phases.B_afterRestart = { chatterinoTextHits: hitsB };
  console.log(JSON.stringify({ phase: "B_afterRestart", hitsB }));
  await ctx.close();
}

// ---- Dump prefs for the extension entry ----
{
  const secure = await readPrefs(profile, "Secure Preferences");
  const plain = await readPrefs(profile, "Preferences");
  const entry = secure?.extensions?.settings?.["bogfpdfoagkaebimmlcbgmfmanhbhhlm"] ?? null;
  result.phases.prefs = {
    secureHasEntry: !!entry,
    location: entry?.location ?? null,
    path: entry?.path ?? null,
    last_update_time: entry?.last_update_time ?? null,
    plainPrefsHasSettings: !!(plain?.extensions?.settings),
  };
  console.log(JSON.stringify({ phase: "prefs", ...result.phases.prefs }));
}

result.verdict = {
  loadedAfterRestart: (result.phases.B_afterRestart?.chatterinoTextHits?.length ?? 0) > 0,
  loadedRightAfterInstall: (result.phases.A_afterLoad?.chatterinoTextHits?.length ?? 0) > 0,
};
console.log(JSON.stringify({ verdict: result.verdict }, null, 2));
console.log(JSON.stringify({ keepProfile: profile }));