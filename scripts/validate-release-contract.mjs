import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const contract = JSON.parse(
  readFileSync(path.join(root, 'release-contract.json'), 'utf8'),
);
const manifest = JSON.parse(
  readFileSync(path.join(root, 'chatterino-extension', 'manifest.json'), 'utf8'),
);
const protocolSource = readFileSync(
  path.join(root, 'chatterino-extension', 'protocol.js'),
  'utf8',
);
const versionMatch = protocolSource.match(/const CURRENT_VERSION = (\d+);/);

if (manifest.version !== contract.extensionVersion) {
  throw new Error(
    `extension version mismatch: manifest=${manifest.version}, contract=${contract.extensionVersion}`,
  );
}
if (Number(versionMatch?.[1]) !== contract.nativeProtocolVersion) {
  throw new Error(
    `protocol version mismatch: implementation=${versionMatch?.[1] || 'missing'}, contract=${contract.nativeProtocolVersion}`,
  );
}

console.log(
  `release contract: extension ${contract.extensionVersion}, protocol v${contract.nativeProtocolVersion}`,
);
