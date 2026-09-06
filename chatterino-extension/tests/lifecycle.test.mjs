import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadLifecycle() {
  const context = { console };
  context.globalThis = context;
  vm.runInNewContext(
    await readFile(
      new URL("../extension-lifecycle.js", import.meta.url),
      "utf8"
    ),
    context
  );
  return context.ChatterinoLifecycle;
}

test("backoff is bounded, exponential, and resettable", async () => {
  const { createBackoffPolicy } = await loadLifecycle();
  const policy = createBackoffPolicy({
    baseMs: 100,
    maxMs: 250,
    maxAttempts: 4,
    clock: () => 1000,
  });
  assert.deepEqual(
    [
      policy.next("disconnect"),
      policy.next("disconnect"),
      policy.next("disconnect"),
    ].map(({ delay, attempt, terminal }) => ({ delay, attempt, terminal })),
    [
      { delay: 100, attempt: 1, terminal: false },
      { delay: 200, attempt: 2, terminal: false },
      { delay: 250, attempt: 3, terminal: false },
    ]
  );
  policy.reset();
  assert.equal(policy.next().attempt, 1);
});

test("session storage rehydrates desired state but never ports, timers, or HWNDs", async () => {
  const { createSessionStore } = await loadLifecycle();
  let saved = {};
  const storage = {
    async get() {
      return { desiredSessions: saved };
    },
    async set(value) {
      saved = value.desiredSessions;
    },
  };
  const first = createSessionStore(storage);
  await first.put({
    sessionId: "browser:1:2",
    windowId: 1,
    tabId: 2,
    channel: "example",
    generation: 7,
    desired: true,
    attached: true,
    port: { unsafe: true },
    timerId: 9,
    browserHwnd: "0x1234",
  });
  const second = createSessionStore(storage);
  const session = await second.get("browser:1:2");
  assert.equal(
    JSON.stringify(session),
    JSON.stringify({
      sessionId: "browser:1:2",
      windowId: 1,
      tabId: 2,
      channel: "example",
      generation: 7,
      desired: true,
      attached: true,
      retryAt: 0,
      retryAttempt: 0,
      leaseExpiresAt: 0,
    })
  );
});

test("diagnostic ring is bounded and redacts session identity", async () => {
  const { createTransitionRing } = await loadLifecycle();
  const ring = createTransitionRing({ limit: 2, clock: () => 42 });
  ring.record("background", "ack", {
    sessionId: "browser:very-secret-channel:tab-12345",
    reason: "chat text must not be copied",
  });
  ring.record("background", "retry", { attempt: 2 });
  ring.record("background", "connected", { protocolVersion: 2 });
  const snapshot = ring.snapshot();
  assert.equal(snapshot.length, 2);
  assert.equal(snapshot[0].event, "retry");
  assert.equal(snapshot[1].event, "connected");
  assert.equal(JSON.stringify(snapshot).includes("very-secret-channel"), false);
});

test("only negotiated protocol-v2 leases expire the overlay", async () => {
  const { leaseDelay } = await loadLifecycle();
  assert.equal(leaseDelay(0, { clock: () => 1000 }), null);
  assert.equal(leaseDelay(undefined, { clock: () => 1000 }), null);
  assert.equal(leaseDelay(9000, { clock: () => 1000 }), 8000);
  assert.equal(leaseDelay(50000, { clock: () => 1000 }), 49000);
});

test("v2 identity is required while legacy v0/v1 remains accepted", async () => {
  const context = { globalThis: {} };
  context.globalThis = context;
  vm.runInNewContext(
    await readFile(new URL("../protocol.js", import.meta.url), "utf8"),
    context
  );
  const protocol = context.ChatterinoProtocol;
  assert.equal(protocol.validate({ action: "select" }).ok, true);
  assert.equal(
    protocol.validate({
      protocolVersion: 2,
      action: "select",
      sessionId: "s",
      generation: 1,
    }).ok,
    false
  );
  assert.equal(
    protocol.validate({
      protocolVersion: 2,
      action: "select",
      sessionId: "s",
      name: "example",
      browserWindowId: 4,
      tabId: 8,
      generation: 1,
    }).ok,
    true
  );
});

test("overlay preserves Twitch DOM and only commits hide after an acknowledgement", async () => {
  const source = await readFile(
    new URL("../overlay.js", import.meta.url),
    "utf8"
  );
  assert.doesNotMatch(source, /children\[0\]\.innerHTML\s*=/);
  assert.match(source, /nativeAttachState/);
  assert.match(source, /phase\s*=\s*["']prepared["']/);
  assert.match(source, /phase\s*=\s*["']attached["']/);
  assert.match(source, /lease-expired/);
});
