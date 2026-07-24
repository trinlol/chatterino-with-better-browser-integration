import { cpSync, existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.join(root, 'chatterino-extension');
const contract = JSON.parse(
  readFileSync(path.join(root, 'release-contract.json'), 'utf8'),
);
const destination = path.resolve(
  root,
  process.argv[2] || path.join('dist', contract.extensionBundleName),
);
const rootPrefix = `${root}${path.sep}`.toLowerCase();

if (!destination.toLowerCase().startsWith(rootPrefix)) {
  throw new Error('extension package destination must stay inside the repository');
}

const runtimeFiles = [
  'activity-state.js',
  'anti-wipe.js',
  'background.js',
  'content.js',
  'icon.png',
  'integration-health.js',
  'manifest.json',
  'overlay.js',
  'page-inject.js',
  'popup.html',
  'popup.js',
  'protocol.js',
  'README.md',
  'styles.css',
  'twitch-api.js',
  'voting-ui.js',
];

if (existsSync(destination)) {
  rmSync(destination, { recursive: true });
}
mkdirSync(destination, { recursive: true });
for (const file of runtimeFiles) {
  cpSync(path.join(source, file), path.join(destination, file));
}

console.log(
  `extension package: ${path.relative(root, destination)} (${contract.extensionVersion})`,
);
