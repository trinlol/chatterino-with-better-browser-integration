// Replicate the real profile's preference files into an isolated profile and
// observe whether Edge auto-loads the unpacked Chatterino extension at startup
// WITHOUT any --load-extension flag. The real profile is never touched.
//
// Signals checked:
//  1. edge://extensions card list (all extension names, not just Chatterino)
//  2. new native-host log files under %TEMP% (the extension SW connects on start)
//  3. Local Extension Settings storage activity for the extension ID
//  4. Secure Preferences entries before/after (did Edge drop the registration?)
//
// Usage: node scripts/browser-integration/repro-replica-profile.mjs
//        [--edge <path-to-msedge.exe>] [--real-profile <Default dir>]
import { chromium } from "playwright";
import { mkdtemp, copyFile, readFile, readdir, stat } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const EDGE_DEFAULT = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const REAL_PROFILE_DEFAULT =
  path.join(process.env.LOCALAPPDATA, "Microsoft", "Edge", "User Data", "Default");

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}
const edgePath = arg("--edge", EDGE_DEFAULT);
const realProfile = path.resolve(arg("--real-profile", REAL_PROFILE_DEFAULT));
const userData = path.dirname(realProfile);

const replica = await mkdtemp(path.join(os.tmpdir(), "chatterino-replica-"));
console.log(JSON.stringify({ phase: "setup", edgePath, realProfile, replica }));

// Copy prefs (Local State lives at the User Data root, not inside Default).
for (const [src, dst] of [
  [path.join(realProfile, "Secure Preferences"), path.join(replica, "Secure Preferences")],
  [path.join(realProfile, "Preferences"), path.join(replica, "Preferences")],
  [path.join(userData, "Local State"), path.join(replica, "Local State")],
]) {
  try {
    await copyFile(src, dst);
    console.log(JSON.stringify({ phase: "copy", src, ok: true }));
  } catch (e) {
    console.log(JSON.stringify({ phase: "copy", src, ok: false, error: e.message }));
  }
}

async function readJson(file) {
  try {
    return JSON.parse(await readFile(file, "utf8"));
  } catch {
    return null;
  }
}

async function prefsEntries() {
  const sp = await readJson(path.join(replica, "Secure Preferences"));
  const settings = sp?.extensions?.settings ?? {};
  return Object.fromEntries(
    Object.entries(settings).map(([id, e]) => [
      id,
      { location: e?.location, last_update: e?.last_update_time },
    ])
  );
}

// Native-host logs + extension storage before launch.
async function hostLogs() {
  try {
    const files = await readdir(os.tmpdir());
    return files
      .filter((f) => /^chatterino-native-host-.*\.log$/.test(f))
      .map(async (f) => {
        const s = await stat(path.join(os.tmpdir(), f));
        return { file: f, mtime: s.mtimeMs };
      });
  } catch {
    return [];
  }
}
const logsBefore = (await Promise.all(await hostLogs())).sort((a, b) => a.mtime - b.mtime);
console.log(JSON.stringify({ phase: "host_logs_before", count: logsBefore.length, newest: logsBefore.at(-1) ?? null }));

const extStorageDir = path.join(replica, "Local Extension Settings", "bogfpdfoagkaebimmlcbgmfmanhbhhlm");
let storageBefore = null;
try {
  const s = await stat(path.join(extStorageDir, "LOG"));
  storageBefore = s.mtimeMs;
} catch { /* not present */ }
console.log(JSON.stringify({ phase: "storage_before", present: !!storageBefore, mtime: storageBefore }));

console.log(JSON.stringify({ phase: "replica_prefs_before", bogfpdfo: (await prefsEntries())["bogfpdfoagkaebimmlcbgmfmanhbhhlm"] }));

const ctx = await chromium.launchPersistentContext(replica, {
  executablePath: edgePath,
  headless: false,
  viewport: { width: 1100, height: 800 },
  ignoreDefaultArgs: [
    "--disable-extensions",
    "--disable-component-extensions-with-background-pages",
    "--disable-default-apps",
  ],
  args: [
    "--no-first-run",
    "--no-default-browser-check",
    "--enable-logging",
    "--v=1",
  ],
});
const page = await ctx.newPage();
page.on("pageerror", (e) => console.log("[pageerror]", e.message));

await page.goto("edge://extensions", { waitUntil: "load", timeout: 30000 });
await page.waitForSelector("root-app", { timeout: 20000 });
await page.waitForTimeout(6000);

const scan = await page.evaluate(() => {
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
  const chatterino = parts.filter((t) => /chatterino/i.test(t)).slice(0, 10);
  const known = ["uBlock", "BetterTTV", "7TV", "Violentmonkey", "Proton", "Enhancer", "Chatterino"];
  const found = known.filter((k) => parts.some((t) => new RegExp(k, "i").test(t)));
  const loadDialog = parts.filter((t) => /failed to load|retry|could not load|error/i.test(t)).slice(0, 20);
  return JSON.stringify({ chatterino, foundKnown: found, loadDialog, bodyLen: parts.length });
});
console.log(JSON.stringify({ phase: "B_afterRestart", url: page.url(), scan: JSON.parse(scan) }));

await ctx.close();

// Post-run signals.
const logsAfter = (await Promise.all(await hostLogs())).sort((a, b) => a.mtime - b.mtime);
const newLogs = logsAfter.filter(
  (l) => !logsBefore.some((b) => b.file === l.file) || l.mtime > (logsBefore.at(-1)?.mtime ?? 0)
);
console.log(JSON.stringify({ phase: "host_logs_after", count: logsAfter.length, newSinceBefore: newLogs }));

let storageAfter = null;
try {
  const s = await stat(path.join(extStorageDir, "LOG"));
  storageAfter = s.mtimeMs;
} catch { /* not present */ }
console.log(JSON.stringify({ phase: "storage_after", present: !!storageAfter, mtime: storageAfter, touched: storageAfter !== storageBefore }));

const afterEntries = await prefsEntries();
console.log(JSON.stringify({
  phase: "replica_prefs_after",
  bogfpdfo: afterEntries["bogfpdfoagkaebimmlcbgmfmanhbhhlm"],
  oenpbjp: afterEntries["oenpbjpibkeomkimhpldpmabdblmipoa"],
  boieha: afterEntries["boiehajcdnhbdmebpnbihmmdfafihlkl"],
}));

const scanObj = JSON.parse(scan);
const verdict = {
  chatterinoCardFound: scanObj.chatterino.length > 0,
  anyKnownCardFound: scanObj.foundKnown.length > 0,
  foundKnown: scanObj.foundKnown,
  hostSpawned: newLogs.length > 0,
  storageTouched: storageAfter !== storageBefore,
  entryStillPresentAfter: !!afterEntries["bogfpdfoagkaebimmlcbgmfmanhbhhlm"],
};
console.log(JSON.stringify({ verdict }, null, 2));

// Dump verbose extension-load lines from Edge's own log.
const debugLog = path.join(replica, "chrome_debug.log");
try {
  const content = await readFile(debugLog, "utf8");
  const lines = content.split(/\r?\n/);
  const hits = lines.filter(
    (l) => /extension|bogfpdfo|oenpbjp|boieha|corrupt|verified|signature|manifest|unpack/i.test(l)
  );
  console.log(JSON.stringify({ phase: "chrome_debug_extension_lines", count: hits.length, sample: hits.slice(0, 60) }, null, 1));
} catch (e) {
  console.log(JSON.stringify({ phase: "chrome_debug_log", error: e.message }));
}
console.log(JSON.stringify({ keepReplica: replica }));