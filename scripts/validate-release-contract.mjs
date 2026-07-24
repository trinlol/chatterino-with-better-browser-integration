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
const cmakeSource = readFileSync(path.join(root, 'CMakeLists.txt'), 'utf8');
const versionHeader = readFileSync(
  path.join(root, 'src', 'common', 'Version.hpp'),
  'utf8',
);
const installerSource = readFileSync(
  path.join(root, '.CI', 'chatterino-installer.iss'),
  'utf8',
);
const versionMatch = protocolSource.match(/const CURRENT_VERSION = (\d+);/);
const cmakeVersionMatch = cmakeSource.match(
  /project\(chatterino[\s\S]*?\bVERSION\s+([^\s)]+)/,
);
const headerNameMatch = versionHeader.match(
  /CHATTERINO_PRODUCT_NAME\s*=\s*\n?\s*QStringLiteral\("([^"]+)"\)/,
);
const headerVersionMatch = versionHeader.match(
  /CHATTERINO_VERSION\s*=\s*QStringLiteral\("([^"]+)"\)/,
);
const installerNameMatch = installerSource.match(
  /#define MyAppName "([^"]+)"/,
);
const installerVersionMatch = installerSource.match(
  /#define MyAppVersion "([^"]+)"/,
);

if (manifest.version !== contract.extensionVersion) {
  throw new Error(
    `extension version mismatch: manifest=${manifest.version}, contract=${contract.extensionVersion}`,
  );
}
for (const [label, actual] of [
  ['CMake application version', cmakeVersionMatch?.[1]],
  ['C++ application version', headerVersionMatch?.[1]],
  ['installer application version', installerVersionMatch?.[1]],
]) {
  if (actual !== contract.applicationVersion) {
    throw new Error(
      `${label} mismatch: implementation=${actual || 'missing'}, contract=${contract.applicationVersion}`,
    );
  }
}
for (const [label, actual] of [
  ['C++ product name', headerNameMatch?.[1]],
  ['installer product name', installerNameMatch?.[1]],
  ['extension product name', manifest.name],
]) {
  if (actual !== contract.applicationName) {
    throw new Error(
      `${label} mismatch: implementation=${actual || 'missing'}, contract=${contract.applicationName}`,
    );
  }
}
if (Number(versionMatch?.[1]) !== contract.nativeProtocolVersion) {
  throw new Error(
    `protocol version mismatch: implementation=${versionMatch?.[1] || 'missing'}, contract=${contract.nativeProtocolVersion}`,
  );
}

console.log(
  `release contract: ${contract.applicationName} ${contract.applicationVersion}, extension ${contract.extensionVersion}, protocol v${contract.nativeProtocolVersion}`,
);
