import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

async function loadProtocol() {
  const context = {};
  context.globalThis = context;
  const source = await readFile(new URL('../protocol.js', import.meta.url), 'utf8');
  vm.runInNewContext(source, context);
  return context.ChatterinoProtocol;
}

test('native messages are upgraded without overwriting the select payload version', async () => {
  const protocol = await loadProtocol();
  const message = protocol.normalizeOutbound({
    action: 'select',
    version: 0,
    name: 'example',
  });

  assert.equal(message.protocolVersion, 1);
  assert.equal(message.version, 0);
});

test('engagement validation rejects incomplete and future messages', async () => {
  const protocol = await loadProtocol();
  assert.equal(
    protocol.validate({
      protocolVersion: 1,
      action: 'engagement',
      lifecycle: 'remove',
      kind: 'poll',
      channel: 'example',
    }).ok,
    true,
  );
  assert.equal(
    protocol.validate({
      protocolVersion: 1,
      action: 'engagement',
      lifecycle: 'upsert',
      kind: 'poll',
      channel: 'example',
    }).ok,
    false,
  );
  assert.equal(
    protocol.validate({ protocolVersion: 2, action: 'sync' }).ok,
    false,
  );
});

test('legacy native messages remain valid as protocol v0', async () => {
  const protocol = await loadProtocol();
  assert.deepEqual(
    { ...protocol.validate({ action: 'prediction' }) },
    { ok: true, version: 0 },
  );
});
