import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

function event(listeners, name) {
  return {
    addListener(listener) {
      listeners[name] = listener;
    },
  };
}

async function settle() {
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
}

async function createHarness({
  sessions,
  tabs,
  response = { ok: true },
  sendError,
} = {}) {
  const listeners = {};
  const nativeMessages = [];
  const sentMessages = [];
  const state = {
    // Seed sessions after worker recovery so each test controls its lease
    // state without racing startup's deliberate reveal transition.
    desiredSessions: {},
  };
  const timerIds = new Map();
  let nextTimerId = 1;
  const storage = {
    async get(key) {
      if (typeof key === "string") return { [key]: state[key] };
      return { ...state };
    },
    async set(values) {
      Object.assign(state, values);
    },
  };
  const port = {
    onMessage: event(listeners, "native.onMessage"),
    onDisconnect: event(listeners, "native.onDisconnect"),
    postMessage(message) {
      nativeMessages.push(message);
    },
    disconnect() {},
  };
  const chrome = {
    windows: {
      WINDOW_ID_NONE: -1,
      get: async () => ({ focused: true, state: "normal" }),
      onRemoved: event(listeners, "windows.onRemoved"),
      onFocusChanged: event(listeners, "windows.onFocusChanged"),
    },
    tabs: {
      get: async (tabId) => tabs.find((tab) => tab.id === tabId) ?? null,
      query: async () => tabs,
      getZoom: async () => 1,
      sendMessage: async (...args) => {
        sentMessages.push(args);
        if (sendError) throw sendError;
        return response;
      },
      onActivated: event(listeners, "tabs.onActivated"),
      onUpdated: event(listeners, "tabs.onUpdated"),
      onDetached: event(listeners, "tabs.onDetached"),
      onCreated: event(listeners, "tabs.onCreated"),
      onRemoved: event(listeners, "tabs.onRemoved"),
    },
    storage: { session: storage, local: storage },
    runtime: {
      connectNative: () => port,
      sendNativeMessage() {},
      onMessage: event(listeners, "runtime.onMessage"),
      getPlatformInfo: async () => ({ os: "win" }),
      lastError: null,
    },
    action: { setBadgeText: async () => {} },
  };
  const context = {
    chrome,
    console: { log() {}, warn() {}, info() {} },
    Date,
    Object,
    Promise,
    Set,
    Map,
    Number,
    String,
    RegExp,
    Error,
    browser: undefined,
    clearTimeout(id) {
      timerIds.delete(id);
    },
    setTimeout(callback, delay) {
      const id = nextTimerId++;
      timerIds.set(id, { callback, delay });
      return id;
    },
  };
  context.globalThis = context;
  for (const filename of [
    "protocol.js",
    "extension-lifecycle.js",
    "background.js",
  ]) {
    vm.runInNewContext(
      await readFile(new URL(`../${filename}`, import.meta.url), "utf8"),
      context,
      { filename }
    );
  }
  await settle();
  // Worker startup deliberately reveals persisted overlays. Restore the
  // supplied live-lease fixtures after that one-time recovery transition.
  for (const session of sessions ?? []) {
    state.desiredSessions[session.sessionId] = { ...session };
  }
  nativeMessages.length = 0;
  sentMessages.length = 0;
  return { listeners, nativeMessages, sentMessages };
}

function v2Command(overrides = {}) {
  return {
    action: "sendNativeChat",
    protocolVersion: 2,
    sessionId: "browser:42:7",
    browserWindowId: 42,
    tabId: 7,
    generation: 4,
    requestId: "request-123",
    channel: "example",
    message: "hello",
    ...overrides,
  };
}

function session(overrides = {}) {
  return {
    sessionId: "browser:42:7",
    windowId: 42,
    tabId: 7,
    channel: "example",
    generation: 4,
    desired: true,
    attached: true,
    leaseExpiresAt: Date.now() + 60_000,
    ...overrides,
  };
}

function tab(id, windowId = 42, channel = "example") {
  return {
    id,
    windowId,
    url: `https://www.twitch.tv/${channel}`,
    highlighted: true,
  };
}

test("v2 chat routing is session-exact even when a channel is open twice", async () => {
  const harness = await createHarness({
    sessions: [
      session(),
      session({ sessionId: "browser:99:8", windowId: 99, tabId: 8 }),
    ],
    tabs: [tab(7), tab(8, 99)],
  });

  harness.listeners["native.onMessage"](v2Command());
  await settle();

  assert.deepEqual(JSON.parse(JSON.stringify(harness.sentMessages)), [
    [
      7,
      {
        action: "sendNativeChat",
        message: "hello",
        sessionId: "browser:42:7",
        browserWindowId: 42,
        tabId: 7,
        generation: 4,
        requestId: "request-123",
      },
    ],
  ]);
  assert.deepEqual(JSON.parse(JSON.stringify(harness.nativeMessages)), [
    {
      action: "nativeChatResult",
      protocolVersion: 2,
      sessionId: "browser:42:7",
      browserWindowId: 42,
      tabId: 7,
      generation: 4,
      requestId: "request-123",
      status: "accepted",
      reason: "delivered",
    },
  ]);
});

test("v2 chat routing rejects stale and unknown session identities without a tab fallback", async () => {
  const stale = await createHarness({ sessions: [session()], tabs: [tab(7)] });
  stale.listeners["native.onMessage"](v2Command({ generation: 3 }));
  await settle();
  assert.equal(stale.sentMessages.length, 0);
  assert.equal(stale.nativeMessages.at(-1).status, "rejected");
  assert.equal(stale.nativeMessages.at(-1).reason, "stale-generation");

  const unknown = await createHarness({
    sessions: [session()],
    tabs: [tab(7)],
  });
  unknown.listeners["native.onMessage"](
    v2Command({ sessionId: "browser:42:404", tabId: 404 })
  );
  await settle();
  assert.equal(unknown.sentMessages.length, 0);
  assert.equal(unknown.nativeMessages.at(-1).status, "rejected");
  assert.equal(unknown.nativeMessages.at(-1).reason, "unknown-session");
});

test("v2 chat routing maps content results and tab delivery exceptions to bounded acknowledgements", async () => {
  const rejected = await createHarness({
    sessions: [session()],
    tabs: [tab(7)],
    response: { ok: false, error: "chat input not found" },
  });
  rejected.listeners["native.onMessage"](v2Command());
  await settle();
  assert.equal(rejected.nativeMessages.at(-1).status, "rejected");
  assert.equal(rejected.nativeMessages.at(-1).reason, "rejected");

  const uncertain = await createHarness({
    sessions: [session()],
    tabs: [tab(7)],
    sendError: new Error("Receiving end does not exist"),
  });
  uncertain.listeners["native.onMessage"](v2Command());
  await settle();
  assert.equal(uncertain.nativeMessages.at(-1).status, "uncertain");
  assert.equal(uncertain.nativeMessages.at(-1).reason, "tab-send-exception");
});

test("legacy channel-only routing is rejected even when an unambiguous tab exists", async () => {
  const harness = await createHarness({
    sessions: [session()],
    tabs: [tab(7)],
  });
  harness.listeners["native.onMessage"]({
    action: "sendNativeChat",
    channel: "example",
    message: "legacy",
  });
  await settle();
  assert.equal(harness.sentMessages.length, 0);
  assert.equal(harness.nativeMessages.length, 0);
});

test("native chat result protocol validation requires exact identity, request, status, and bounded reason", async () => {
  const context = { globalThis: {} };
  context.globalThis = context;
  vm.runInNewContext(
    await readFile(new URL("../protocol.js", import.meta.url), "utf8"),
    context
  );
  const result = {
    action: "nativeChatResult",
    protocolVersion: 2,
    sessionId: "browser:42:7",
    browserWindowId: 42,
    tabId: 7,
    generation: 4,
    requestId: "request-123",
    status: "accepted",
    reason: "delivered",
  };
  assert.equal(context.ChatterinoProtocol.validate(result).ok, true);
  for (const invalid of [
    { ...result, requestId: "" },
    { ...result, requestId: "request id" },
    { ...result, status: "ok" },
    { ...result, reason: "user supplied detail" },
    { ...result, tabId: undefined },
    { ...result, browserWindowId: "not-a-window" },
  ]) {
    assert.equal(context.ChatterinoProtocol.validate(invalid).ok, false);
  }
});
