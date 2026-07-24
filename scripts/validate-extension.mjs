import { spawnSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const extensionDir = path.join(root, "chatterino-extension");
const manifestPath = path.join(extensionDir, "manifest.json");

function fail(message) {
  console.error(`extension validation: ${message}`);
  process.exitCode = 1;
}

function walk(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const absolute = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(absolute) : [absolute];
  });
}

function referencedManifestFiles(manifest) {
  const files = new Set();
  for (const script of manifest.content_scripts ?? []) {
    for (const file of script.js ?? []) files.add(file);
    for (const file of script.css ?? []) files.add(file);
  }
  if (manifest.background?.service_worker) {
    files.add(manifest.background.service_worker);
  }
  if (manifest.action?.default_popup) {
    files.add(manifest.action.default_popup);
  }
  for (const resourceGroup of manifest.web_accessible_resources ?? []) {
    for (const file of resourceGroup.resources ?? []) files.add(file);
  }
  for (const file of Object.values(manifest.icons ?? {})) files.add(file);
  return [...files];
}

let manifest;
try {
  manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
} catch (error) {
  fail(`manifest.json is not valid JSON: ${error.message}`);
}

if (manifest) {
  if (manifest.manifest_version !== 3) {
    fail(`expected manifest_version 3, got ${manifest.manifest_version}`);
  }
  for (const relative of referencedManifestFiles(manifest)) {
    if (!existsSync(path.join(extensionDir, relative))) {
      fail(`manifest references missing file: ${relative}`);
    }
  }
}

for (const file of walk(extensionDir).filter((item) =>
  /\.(?:js|mjs)$/.test(item)
)) {
  const checked = spawnSync(process.execPath, ["--check", file], {
    cwd: root,
    encoding: "utf8",
  });
  if (checked.status !== 0) {
    fail(
      `syntax check failed for ${path.relative(root, file)}\n${checked.stderr}`
    );
  }
}

if (process.exitCode) process.exit(process.exitCode);

const testFiles = walk(path.join(extensionDir, "tests"))
  .filter((item) => item.endsWith(".test.mjs"))
  .map((item) => path.relative(root, item));
const tests = spawnSync(process.execPath, ["--test", ...testFiles], {
  cwd: root,
  encoding: "utf8",
  stdio: "inherit",
});

if (tests.status !== 0) {
  process.exit(tests.status ?? 1);
}

console.log("extension validation: manifest, syntax, and tests passed");
