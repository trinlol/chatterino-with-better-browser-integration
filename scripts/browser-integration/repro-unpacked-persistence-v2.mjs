// Fresh-profile reproduction of the user's exact flow, fully self-service:
//   Phase A: fresh temp profile -> drive Load unpacked UI (native folder
//            dialog handled via SendKeys) -> verify card appears.
//   Phase B: close -> relaunch SAME profile with NO extension flags -> is the
//            unpacked extension still listed/loaded?
//
// Usage: node scripts/browser-integration/repro-unpacked-persistence-v2.mjs
//        [--edge <exe>] [--extension <dir>]
import { chromium } from "playwright";
import { spawn } from "node:child_process";
import { mkdtemp, readFile } from "node:fs/promises";
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
const profile = await mkdtemp(path.join(os.tmpdir(), "chatterino-fresh-"));
console.log(JSON.stringify({ phase: "setup", edgePath, extDir, profile }));

function pickFolderViaDialog(dir) {
  // Embed the path directly: powershell -Command does not pass trailing args
  // into $args the way -File does.
  const script = `
Add-Type -AssemblyName System.Windows.Forms | Out-Null
$ws = New-Object -ComObject WScript.Shell
$ok = $false
foreach ($title in @('Select the extension directory','Select folder','Select a folder')) {
  if ($ws.AppActivate($title)) { $ok = $true; break }
}
if (-not $ok) { Write-Output 'APPACTIVATE_FAIL'; exit 1 }
Start-Sleep -Milliseconds 300
$ws.SendKeys('^l')
Start-Sleep -Milliseconds 300
$ws.SendKeys('${dir}')
Start-Sleep -Milliseconds 400
$ws.SendKeys('{ENTER}')
Start-Sleep -Milliseconds 1500
$ws.SendKeys('{ENTER}')
Start-Sleep -Milliseconds 800
$ws.SendKeys('{ENTER}')
Write-Output 'SENDKEYS_DONE'
`;
  return new Promise((resolve, reject) => {
    const ps = spawn(
      "powershell",
      ["-NoProfile", "-NonInteractive", "-Command", script],
      { stdio: ["ignore", "pipe", "pipe"], windowsHide: true }
    );
    let out = "";
    ps.stdout.on("data", (d) => (out += d));
    ps.stderr.on("data", (d) => (out += d));
    ps.on("error", reject);
    ps.on("close", (code) => resolve({ code, out: out.trim() }));
  });
}

async function launch() {
  return chromium.launchPersistentContext(profile, {
    executablePath: edgePath,
    headless: false,
    viewport: { width: 1100, height: 800 },
    ignoreDefaultArgs: ["--disable-extensions", "--disable-component-extensions-with-background-pages"],
    args: ["--no-first-run", "--no-default-browser-check"],
  });
}

async function enableDevMode(page) {
  await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
  await page.waitForSelector("root-app", { timeout: 20000 });
  const sw = page.locator("developer-mode-switch").first();
  await sw.waitFor({ state: "attached", timeout: 20000 });
  await sw.evaluate((el) => el.shadowRoot.querySelector("fluent-switch").click());
  await page.waitForTimeout(800);
  const checked = await sw.evaluate((el) =>
    el.shadowRoot?.querySelector("fluent-switch")?.getAttribute("checked"));
  if (checked !== "true") throw new Error("developer mode did not enable");
}

async function collectTexts(page) {
  return page.evaluate(() => {
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
  });
}

const result = { profile, phases: {} };

// ---- Phase A: load unpacked via UI + native dialog ----
{
  const ctx = await launch();
  const page = await ctx.newPage();
  await enableDevMode(page);
  // Click Load unpacked; the native folder dialog opens. The button lives in a
  // closed menu (no bounding box), so fire a native DOM click on the label.
  const btn = page.getByText("Load unpacked", { exact: true }).first();
  await btn.evaluate((el) => el.click());
  await new Promise((r) => setTimeout(r, 1500));
  const sk = await pickFolderViaDialog(extDir);
  console.log(JSON.stringify({ phase: "sendkeys", ...sk }));
  // Check for a lingering native dialog.
  const dlg = await new Promise((resolve) => {
    const ps = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command",
      "Get-Process | Where-Object { $_.MainWindowTitle } | Select-Object -ExpandProperty MainWindowTitle"], 
      { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    let out = "";
    ps.stdout.on("data", (d) => (out += d));
    ps.stderr.on("data", (d) => (out += d));
    ps.on("close", () => resolve(out.trim().split(/\r?\n/).filter(Boolean)));
  });
  console.log(JSON.stringify({ phase: "open_windows_after_dialog", titles: dlg }));
  await page.waitForTimeout(3000);
  const textsA = await collectTexts(page);
  result.phases.A_chatterinoHits = textsA.filter((t) => /chatterino|replaces twitch/i.test(t)).slice(0, 10);
  result.phases.A_bodyLen = textsA.length;
  result.phases.A_allText = textsA;
  console.log(JSON.stringify({ phase: "A_afterLoad", result: result.phases.A_chatterinoHits, bodyLen: result.phases.A_bodyLen, all: textsA }));
  await ctx.close();
  await new Promise((r) => setTimeout(r, 1500));
}

// ---- Phase B: relaunch same profile, no flags ----
{
  const ctx = await launch();
  const page = await ctx.newPage();
  await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
  await page.waitForSelector("root-app", { timeout: 20000 });
  await page.waitForTimeout(4000);
  const textsB = await collectTexts(page);
  result.phases.B_chatterinoHits = textsB.filter((t) => /chatterino|replaces twitch/i.test(t)).slice(0, 10);
  result.phases.B_bodyLen = textsB.length;
  console.log(JSON.stringify({ phase: "B_afterRestart", result: result.phases.B_chatterinoHits, bodyLen: result.phases.B_bodyLen }));
  await ctx.close();
}

// ---- prefs snapshot ----
try {
  const sp = JSON.parse(await readFile(path.join(profile, "Secure Preferences"), "utf8"));
  const entries = Object.fromEntries(
    Object.entries(sp?.extensions?.settings ?? {}).map(([id, e]) => [id, { location: e?.location }])
  );
  result.phases.prefsEntries = entries;
  console.log(JSON.stringify({ phase: "prefs", entries }));
} catch (e) {
  result.phases.prefsError = e.message;
}

result.verdict = {
  loadedAfterRestart: result.phases.B_chatterinoHits?.length > 0,
  loadedRightAfterInstall: result.phases.A_chatterinoHits?.length > 0,
  aBodyLen: result.phases.A_bodyLen,
  bBodyLen: result.phases.B_bodyLen,
};
console.log(JSON.stringify({ verdict: result.verdict }, null, 2));
console.log(JSON.stringify({ keepProfile: profile }));