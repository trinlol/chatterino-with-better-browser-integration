import assert from "node:assert/strict";
import test from "node:test";

import {
  MAX_NATIVE_MESSAGE_BYTES,
  createNativeMessageDecoder,
  encodeNativeMessage,
  startFakeNativeHost,
} from "../../../scripts/browser-integration/native-messaging-peer.mjs";
import { lifecycleScenarios } from "../../../scripts/browser-integration/scenarios.mjs";

async function withHost(scenario, run) {
  const host = await startFakeNativeHost(scenario);
  try {
    await run(host);
  } finally {
    host.close();
    await host.exit;
  }
}

test("native framing survives chunk boundaries and rejects oversized frames", () => {
  const messages = [];
  const decode = createNativeMessageDecoder((message) =>
    messages.push(message)
  );
  const frame = encodeNativeMessage({ action: "select", winId: "42" });
  decode(frame.subarray(0, 2));
  decode(frame.subarray(2, 9));
  decode(frame.subarray(9));
  assert.deepEqual(messages, [{ action: "select", winId: "42" }]);

  const oversized = Buffer.alloc(4);
  oversized.writeUInt32LE(MAX_NATIVE_MESSAGE_BYTES + 1, 0);
  assert.throws(() => decode(oversized), /exceeds/);
});

test("browser-first and desktop-first startup emit a readiness signal and exact acknowledgement", async () => {
  for (const name of ["browser-first", "desktop-first"]) {
    await withHost(lifecycleScenarios[name], async (host) => {
      await host.waitFor((frame) => frame.type === "status");
      host.send({
        action: "select",
        winId: "42",
        attachRequestId: `${name}-request`,
      });
      const ack = await host.waitFor(
        (frame) => frame.status === "chat-attached"
      );
      assert.equal(ack.attachRequestId, `${name}-request`);
    });
  }
});

test("stale acknowledgement arrives before the current acknowledgement", async () => {
  await withHost(lifecycleScenarios["stale-ack"], async (host) => {
    host.send({
      action: "select",
      winId: "42",
      attachRequestId: "current-request",
    });
    const stale = await host.waitFor(
      (frame) => frame.attachRequestId === "stale-request"
    );
    const current = await host.waitFor(
      (frame) => frame.attachRequestId === "current-request"
    );
    assert.equal(stale.status, "chat-attached");
    assert.equal(current.status, "chat-attached");
    assert.ok(host.received.indexOf(stale) < host.received.indexOf(current));
  });
});

test("host death, navigation, and worker recreation are executable fault fixtures", async () => {
  const dying = await startFakeNativeHost(lifecycleScenarios["host-death"]);
  dying.send({ action: "select" });
  assert.deepEqual(await dying.exit, { code: 0, signal: null });

  await withHost(lifecycleScenarios.navigation, async (host) => {
    host.send({ action: "detach", winId: "42" });
    await host.waitFor((frame) => frame.status === "chat-detached");
  });

  await withHost(lifecycleScenarios["worker-recreation"], async (host) => {
    await host.waitFor((frame) => frame.status === "native-host-ready");
    host.send({ action: "select", attachRequestId: "worker-recreated" });
    const ack = await host.waitFor((frame) => frame.status === "chat-attached");
    assert.equal(ack.attachRequestId, "worker-recreated");
  });
});

test("two same-channel windows keep acknowledgement identity and fail-open reject is timely", async () => {
  await withHost(
    lifecycleScenarios["two-windows-same-channel"],
    async (host) => {
      host.send({
        action: "select",
        name: "example",
        winId: "42",
        attachRequestId: "window-a",
      });
      host.send({
        action: "select",
        name: "example",
        winId: "43",
        attachRequestId: "window-b",
      });
      await host.waitFor((frame) => frame.attachRequestId === "window-a");
      await host.waitFor((frame) => frame.attachRequestId === "window-b");
    }
  );

  await withHost(lifecycleScenarios["fail-open-timing"], async (host) => {
    const started = performance.now();
    host.send({ action: "select", attachRequestId: "must-reveal" });
    const rejection = await host.waitFor(
      (frame) => frame.status === "chat-rejected",
      1000
    );
    assert.equal(rejection.reason, "fake-host-reject");
    assert.ok(
      performance.now() - started < 2000,
      "fallback contract is under two seconds"
    );
  });
});
