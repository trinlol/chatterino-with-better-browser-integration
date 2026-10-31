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
  for (let i = 0; i < 4; i += 1) {
    await new Promise((resolve) => setImmediate(resolve));
  }
}

async function createHarness({
  sessions = [],
  tabs = [],
  response,
  alarms,
} = {}) {
  const listeners = {};
  const nativeMessages = [];
  const oneShotMessages = [];
  const sentMessages = [];
  const state = { desiredSessions: {} };
  const timers = new Map();
  const clock = { now: Date.now() };
  let nextTimerId = 1;
  let connects = 0;
  const storage = {
    async get(key) {
      if (typeof key === "string") return { [key]: state[key] };
      return { ...state };
    },
    async set(values) {
      Object.assign(state, values);
    },
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
        return response ?? { ok: true };
      },
      onActivated: event(listeners, "tabs.onActivated"),
      onUpdated: event(listeners, "tabs.onUpdated"),
      onDetached: event(listeners, "tabs.onDetached"),
      onCreated: event(listeners, "tabs.onCreated"),
      onRemoved: event(listeners, "tabs.onRemoved"),
    },
    storage: { session: storage, local: storage },
    runtime: {
      id: "own-extension",
      connectNative: () => {
        connects += 1;
        return {
          onMessage: event(listeners, "native.onMessage"),
          onDisconnect: event(listeners, "native.onDisconnect"),
          postMessage(message) {
            nativeMessages.push(message);
          },
          disconnect() {},
        };
      },
      sendNativeMessage(_name, message) {
        oneShotMessages.push(message);
      },
      onMessage: event(listeners, "runtime.onMessage"),
      getPlatformInfo: async () => ({ os: "win" }),
      lastError: null,
    },
    action: { setBadgeText: async () => {} },
    ...(alarms ? { alarms } : {}),
  };
  class FakeDate extends Date {
    static now() {
      return clock.now;
    }
  }
  const context = {
    chrome,
    console: { log() {}, warn() {}, info() {} },
    Date: FakeDate,
    Object,
    Promise,
    Set,
    Map,
    Number,
    String,
    RegExp,
    Error,
    JSON,
    browser: undefined,
    crypto: globalThis.crypto,
    clearTimeout(id) {
      timers.delete(id);
    },
    setTimeout(callback, delay) {
      const id = nextTimerId++;
      timers.set(id, { callback, delay });
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
  for (const session of sessions) {
    state.desiredSessions[session.sessionId] = { ...session };
  }
  nativeMessages.length = 0;
  sentMessages.length = 0;
  return {
    listeners,
    nativeMessages,
    oneShotMessages,
    sentMessages,
    state,
    timers,
    clock,
    get connects() {
      return connects;
    },
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

function twitchTab(id, windowId = 42, channel = "example") {
  return {
    id,
    windowId,
    active: true,
    highlighted: true,
    url: `https://www.twitch.tv/${channel}`,
  };
}

const plain = (value) => JSON.parse(JSON.stringify(value));

test("native minute-watched requests never reach a tab that left Twitch, and results reach the host", async () => {
  // Without the `tabs` permission a non-Twitch tab reports an empty URL.
  const harness = await createHarness({
    sessions: [session()],
    tabs: [{ id: 7, windowId: 42, active: true, url: "" }],
  });
  harness.listeners["native.onMessage"]({
    action: "sendMinuteWatched",
    protocolVersion: 2,
    sessionId: "browser:42:7",
    channel: "example",
    requestId: "minute-1",
  });
  await settle();
  assert.equal(harness.sentMessages.length, 0);
  const result = harness.nativeMessages.at(-1);
  assert.equal(result?.action, "minuteWatchedResult");
  assert.equal(result.status, "rejected");
  assert.equal(result.reason, "tab-not-found");
});

test("minute-watched requests are delivered to the session's live Twitch tab", async () => {
  const harness = await createHarness({
    sessions: [session()],
    tabs: [twitchTab(7)],
  });
  harness.listeners["native.onMessage"]({
    action: "sendMinuteWatched",
    protocolVersion: 2,
    sessionId: "browser:42:7",
    channel: "example",
    requestId: "minute-2",
  });
  await settle();
  assert.equal(harness.sentMessages.length, 1);
  assert.equal(harness.sentMessages[0][0], 7);
  assert.equal(harness.sentMessages[0][1].action, "sendMinuteWatched");
});

test("content-script minute-watched results are forwarded to the native host", async () => {
  const harness = await createHarness({ tabs: [twitchTab(7)] });
  harness.listeners["runtime.onMessage"](
    { action: "minuteWatchedResult", ok: true, channel: "example" },
    { id: "own-extension", tab: twitchTab(7) },
    () => {}
  );
  await settle();
  assert.equal(harness.nativeMessages.at(-1)?.action, "minuteWatchedResult");
});

test("a lease renewal racing a detach does not re-hide Twitch chat", async () => {
  const harness = await createHarness({
    sessions: [session({ desired: false, attached: false, leaseExpiresAt: 0 })],
    tabs: [twitchTab(7)],
  });
  harness.listeners["native.onMessage"]({
    type: "status",
    status: "lease-renewed",
    sessionId: "browser:42:7",
    generation: 4,
    leaseExpiresAt: Date.now() + 120_000,
  });
  await settle();
  assert.equal(harness.sentMessages.length, 0);
  assert.equal(
    harness.state.desiredSessions["browser:42:7"].attached,
    false
  );
});

test("native chat results are validated and only routed to the session's Twitch tab", async () => {
  const result = {
    action: "nativeChatResult",
    sessionId: "browser:42:7",
    generation: 4,
    requestId: "request-1",
    status: "accepted",
    reason: "delivered",
  };
  const harness = await createHarness({
    sessions: [session()],
    tabs: [twitchTab(7)],
  });
  harness.listeners["native.onMessage"]({ ...result, status: "<script>" });
  harness.listeners["native.onMessage"]({ ...result, reason: "x".repeat(500) });
  await settle();
  assert.equal(harness.sentMessages.length, 0);
  harness.listeners["native.onMessage"](result);
  await settle();
  assert.equal(harness.sentMessages.length, 1);

  const moved = await createHarness({
    sessions: [session()],
    tabs: [twitchTab(7, 42, "otherchannel")],
  });
  moved.listeners["native.onMessage"](result);
  await settle();
  assert.equal(moved.sentMessages.length, 0);
});

test("native chat sends with oversized or non-string text are rejected", async () => {
  const harness = await createHarness({
    sessions: [session()],
    tabs: [twitchTab(7)],
  });
  for (const message of ["x".repeat(501), { text: "hi" }]) {
    harness.listeners["native.onMessage"]({
      action: "sendNativeChat",
      protocolVersion: 2,
      sessionId: "browser:42:7",
      browserWindowId: 42,
      tabId: 7,
      generation: 4,
      requestId: "request-big",
      channel: "example",
      message,
    });
    await settle();
  }
  assert.equal(harness.sentMessages.length, 0);
  assert.deepEqual(
    plain(harness.nativeMessages.map((m) => m.reason)),
    ["invalid-message", "invalid-message"]
  );
});

test("reconnect backoff grows while the native host keeps dying on launch", async () => {
  const harness = await createHarness();
  const delays = [];
  for (let i = 0; i < 4; i += 1) {
    harness.timers.clear();
    harness.listeners["native.onDisconnect"]();
    const [timer] = [...harness.timers.values()];
    delays.push(timer.delay);
    harness.clock.now += timer.delay;
    timer.callback();
  }
  assert.deepEqual(delays, [5000, 10000, 20000, 40000]);

  // A port that delivers a message is healthy; the next failure starts over.
  harness.listeners["native.onMessage"]({ type: "status", status: "x" });
  harness.timers.clear();
  harness.listeners["native.onDisconnect"]();
  assert.equal([...harness.timers.values()][0].delay, 5000);
});

test("content scripts cannot change settings and unknown setting keys answer cleanly", async () => {
  const harness = await createHarness({ tabs: [twitchTab(7)] });
  harness.listeners["runtime.onMessage"](
    { type: "set-setting", key: "replaceTwitchChat", value: false },
    { id: "own-extension", tab: twitchTab(7) },
    () => {}
  );
  harness.listeners["runtime.onMessage"](
    { type: "set-setting", key: "__proto__", value: true },
    { id: "own-extension" },
    () => {}
  );
  await settle();
  assert.equal("replaceTwitchChat" in harness.state, false);

  harness.listeners["runtime.onMessage"](
    { type: "set-setting", key: "replaceTwitchChat", value: false },
    { id: "own-extension" },
    () => {}
  );
  await settle();
  assert.equal(harness.state.replaceTwitchChat, false);

  let answer = "pending";
  harness.listeners["runtime.onMessage"](
    { type: "get-setting", key: "constructor" },
    { id: "own-extension", tab: twitchTab(7) },
    (value) => {
      answer = value;
    }
  );
  await settle();
  assert.equal(answer, undefined);
});

test("malformed chat geometry and tab-less senders are ignored without throwing", async () => {
  const harness = await createHarness({ tabs: [twitchTab(7)] });
  const onMessage = harness.listeners["runtime.onMessage"];
  for (const rect of [
    undefined,
    { x: 0, width: Number.NaN, height: 720 },
    { x: 0, width: 340, height: 1e9 },
  ]) {
    onMessage(
      { type: "chat-resized", rect, dpr: 1 },
      { id: "own-extension", tab: twitchTab(7) },
      () => {}
    );
  }
  onMessage({ type: "chat-resized", rect: { x: 0, width: 1, height: 1 } }, {}, () => {});
  onMessage({ type: "location-updated" }, {}, () => {});
  onMessage({ type: "detach" }, {}, () => {});
  await settle();
  assert.equal(harness.sentMessages.length, 0);
  assert.equal(harness.nativeMessages.length, 0);
});

test("worker wake-ups keep the existing reconcile alarm instead of restarting it", async () => {
  const created = [];
  const makeAlarms = (existing) => ({
    onAlarm: { addListener() {} },
    get: async (name) => existing[name],
    create: async (name, info) => {
      created.push(name);
      existing[name] = info;
    },
  });
  const existing = {};
  await createHarness({ alarms: makeAlarms(existing) });
  await createHarness({ alarms: makeAlarms(existing) });
  assert.deepEqual(created, ["chatterino-reconcile"]);
});

test("a collapsed chat (zero width) is still forwarded so the desktop can hide its window", async () => {
  const harness = await createHarness({ tabs: [twitchTab(7)] });
  const onMessage = harness.listeners["runtime.onMessage"];
  onMessage(
    { type: "chat-resized", rect: { x: 0, width: 340, height: 720 }, dpr: 1 },
    { id: "own-extension", tab: twitchTab(7) },
    () => {}
  );
  await settle();
  const before = harness.nativeMessages.length;
  assert.ok(before > 0, "a normal geometry reaches the host");
  onMessage(
    { type: "chat-resized", rect: { x: 0, width: 0, height: 720 }, dpr: 1 },
    { id: "own-extension", tab: twitchTab(7) },
    () => {}
  );
  await settle();
  assert.ok(harness.nativeMessages.length > before, "zero-width geometry reaches the host");
});
