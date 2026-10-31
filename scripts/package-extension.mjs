import { cpSync, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = path.join(root, "chatterino-extension");
const contract = JSON.parse(
  readFileSync(path.join(root, "release-contract.json"), "utf8")
);
const destination = path.resolve(
  root,
  process.argv[2] || path.join("dist", contract.extensionBundleName)
);
const rootPrefix = `${root}${path.sep}`.toLowerCase();

if (!destination.toLowerCase().startsWith(rootPrefix)) {
  throw new Error(
    "extension package destination must stay inside the repository"
  );
}

// Everything the browser loads is derived from manifest.json, so a new
// content script can never be left out of the package (a missing file makes
// Chrome refuse to load the whole extension).
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
// The service worker pulls helpers in with importScripts().
const worker = manifest.background?.service_worker;
if (worker) {
  const code = readFileSync(path.join(source, worker), "utf8");
  for (const call of code.matchAll(/importScripts\(([^)]*)\)/g)) {
    for (const name of call[1].matchAll(/["']([^"']+\.js)["']/g)) {
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

console.log(
  `extension package: ${path.relative(root, destination)} (${contract.extensionVersion})`
);
