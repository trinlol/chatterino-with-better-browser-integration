// Open the Load unpacked native dialog and enumerate window titles/classes.
import { chromium } from "playwright";
import { spawn } from "node:child_process";
import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const profile = await mkdtemp(path.join(os.tmpdir(), "chatterino-dlg-"));
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
await page.waitForTimeout(800);
await page.getByText("Load unpacked", { exact: true }).first().evaluate((el) => el.click());
await new Promise((r) => setTimeout(r, 2500));

const winInfo = await new Promise((resolve) => {
  const ps = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command",
    "Get-Process | Where-Object { $_.MainWindowTitle -ne '' } | ForEach-Object { \"$($_.ProcessName)|$($_.MainWindowTitle)\" }"
  ], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
  let out = "";
  ps.stdout.on("data", (d) => (out += d));
  ps.stderr.on("data", (d) => (out += d));
  ps.on("close", () => resolve(out.trim().split(/\r?\n/).filter(Boolean)));
});
console.log(JSON.stringify({ phase: "windows_with_dialog_open", windows: winInfo }, null, 1));
await ctx.close();