import {
  cpSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = path.join(root, "chatterino-extension");
const contract = JSON.parse(
  readFileSync(path.join(root, "release-contract.json"), "utf8")
);

const cliArgs = process.argv.slice(2);
let target = "chromium";
let explicitDestination = "";
for (const arg of cliArgs) {
  if (arg.startsWith("--target=")) {
    target = arg.slice("--target=".length).toLowerCase();
  } else if (!arg.startsWith("-")) {
    explicitDestination = arg;
  }
}
if (target !== "chromium" && target !== "firefox") {
  throw new Error(
    `extension package: unsupported --target=${target} (expected chromium or firefox)`
  );
}

const defaultDirName =
  target === "firefox"
    ? `${contract.extensionBundleName}-firefox`
    : contract.extensionBundleName;
const destination = path.resolve(
  root,
  explicitDestination || path.join("dist", defaultDirName)
);
const rootPrefix = `${root}${path.sep}`.toLowerCase();

if (!destination.toLowerCase().startsWith(rootPrefix)) {
  throw new Error(
    "extension package destination must stay inside the repository"
  );
}

// Everything the browser loads is derived from manifest.json, so a new
// content script can never be left out of the package (a missing file makes
// Chrome/Firefox refuse to load the whole extension).
const manifest = JSON.parse(
  readFileSync(path.join(source, "manifest.json"), "utf8")
);
const runtimeFiles = new Set(["manifest.json", "README.md"]);
for (const script of manifest.content_scripts ?? []) {
  for (const file of [...(script.js ?? []), ...(script.css ?? [])]) {
    runtimeFiles.add(file);
  }
}
if (manifest.background?.service_worker) {
  runtimeFiles.add(manifest.background.service_worker);
}
for (const entry of manifest.web_accessible_resources ?? []) {
  for (const file of entry.resources ?? []) runtimeFiles.add(file);
}
for (const file of Object.values(manifest.icons ?? {})) runtimeFiles.add(file);
const popup = manifest.action?.default_popup;
if (popup) {
  runtimeFiles.add(popup);
  // Scripts and stylesheets the popup references locally.
  const html = readFileSync(path.join(source, popup), "utf8");
  for (const match of html.matchAll(/(?:src|href)="([^":?#]+\.(?:js|css))"/g)) {
    runtimeFiles.add(match[1]);
  }
}
// The service worker pulls helpers in with importScripts(). Preserve load order
// so Firefox's background.scripts event page loads them before background.js.
const importedWorkerScripts = [];
const worker = manifest.background?.service_worker;
if (worker) {
  const code = readFileSync(path.join(source, worker), "utf8");
  for (const call of code.matchAll(/importScripts\(([^)]*)\)/g)) {
    for (const name of call[1].matchAll(/["']([^"']+\.js)["']/g)) {
      if (!importedWorkerScripts.includes(name[1])) {
        importedWorkerScripts.push(name[1]);
      }
      runtimeFiles.add(name[1]);
    }
  }
}

const missing = [...runtimeFiles].filter(
  (file) => !existsSync(path.join(source, file))
);
if (missing.length) {
  throw new Error(`extension package: missing files ${missing.join(", ")}`);
}

if (existsSync(destination)) {
  rmSync(destination, { recursive: true });
}
mkdirSync(destination, { recursive: true });
for (const file of runtimeFiles) {
  cpSync(path.join(source, file), path.join(destination, file));
}

if (target === "firefox") {
  const firefoxManifest = structuredClone(manifest);
  // Chrome's public key and editor $schema field trigger warnings in Firefox.
  delete firefoxManifest.key;
  delete firefoxManifest.$schema;
  if (worker) {
    firefoxManifest.background = {
      scripts: [...importedWorkerScripts, worker],
    };
  }
  const gecko = firefoxManifest.browser_specific_settings?.gecko;
  if (!gecko?.id || gecko.id === "chatterino_native@chatterino.com") {
    throw new Error(
      "extension package (firefox): browser_specific_settings.gecko.id must be set to this fork's own AMO add-on ID"
    );
  }
  writeFileSync(
    path.join(destination, "manifest.json"),
    `${JSON.stringify(firefoxManifest, null, 2)}\n`,
    "utf8"
  );

  // Build an AMO-ready zip archive (manifest.json at archive root).
  const zipPath = path.join(
    path.dirname(destination),
    `${contract.extensionBundleName}-firefox-v${contract.extensionVersion}.zip`
  );
  if (existsSync(zipPath)) {
    rmSync(zipPath, { force: true });
  }
  if (process.platform === "win32") {
    const zipCmd = spawnSync(
      "powershell",
      [
        "-NoProfile",
        "-Command",
        `Compress-Archive -Path '${destination}\\*' -DestinationPath '${zipPath}' -CompressionLevel Optimal -Force`,
      ],
      { stdio: "inherit" }
    );
    if (zipCmd.status !== 0) {
      throw new Error("extension package (firefox): failed to create zip");
    }
  } else {
    const zipCmd = spawnSync("zip", ["-rq", zipPath, "."], {
      cwd: destination,
      stdio: "inherit",
    });
    if (zipCmd.status !== 0) {
      throw new Error("extension package (firefox): failed to create zip");
    }
  }
  console.log(
    `extension package (firefox): ${path.relative(root, destination)} + ${path.relative(root, zipPath)} (${contract.extensionVersion})`
  );
} else {
  console.log(
    `extension package: ${path.relative(root, destination)} (${contract.extensionVersion})`
  );
}
