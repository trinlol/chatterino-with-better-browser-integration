import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const browserHostSource = await readFile(
  new URL("../../src/BrowserExtension.cpp", import.meta.url),
  "utf8"
);

function createEvent(listeners, name) {
  return {
    addListener(listener) {
      listeners[name] = listener;
    },
  };
}

function createBackgroundHarness({ windowFocused = true } = {}) {
  const listeners = {};
  const sentMessages = [];
  const nativeMessages = [];
  const timers = [];
  let nextTimerId = 1;
  const activeTab = {
    id: 7,
    windowId: 42,
    url: "https://www.twitch.tv/example",
    highlighted: true,
  };

  return {
    listeners,
    sentMessages,
    nativeMessages,
    timers,
    setTimeout(callback, delay) {
      const timer = {
        callback,
        cleared: false,
        delay,
        id: nextTimerId++,
      };
      timers.push(timer);
      return timer.id;
    },
    clearTimeout(id) {
      const timer = timers.find((candidate) => candidate.id === id);
      if (timer) timer.cleared = true;
    },
    chrome: {
      windows: {
        WINDOW_ID_NONE: -1,
        get: async () => ({ focused: windowFocused, state: "normal" }),
        onRemoved: createEvent(listeners, "windows.onRemoved"),
        onFocusChanged: createEvent(listeners, "windows.onFocusChanged"),
      },
      tabs: {
        get: async () => activeTab,
        query: async (queryInfo) =>
          (queryInfo.active && queryInfo.currentWindow) || queryInfo.url
            ? [activeTab]
            : [],
        getZoom: async () => 1,
        sendMessage: async (...args) => sentMessages.push(args),
        onActivated: createEvent(listeners, "tabs.onActivated"),
        onUpdated: createEvent(listeners, "tabs.onUpdated"),
        onDetached: createEvent(listeners, "tabs.onDetached"),
        onCreated: createEvent(listeners, "tabs.onCreated"),
        onRemoved: createEvent(listeners, "tabs.onRemoved"),
      },
      storage: {
        session: { get: async () => ({}), set: async () => {} },
        local: { get: async () => ({}), set: async () => {} },
      },
      runtime: {
        connectNative: () => ({
          onMessage: createEvent(listeners, "native.onMessage"),
          onDisconnect: createEvent(listeners, "native.onDisconnect"),
          postMessage(message) {
            nativeMessages.push(message);
          },
        }),
        sendNativeMessage() {},
        onMessage: createEvent(listeners, "runtime.onMessage"),
        getPlatformInfo: (callback) => {
          const info = { os: "win" };
          if (callback) {
            callback(info);
            return;
          }
          return Promise.resolve(info);
        },
        lastError: null,
      },
      action: { setBadgeText: async () => {} },
    },
  };
}

test("activating a Twitch channel asks the overlay for fresh chat geometry", async () => {
  const harness = createBackgroundHarness();
  const source = await readFile(
    new URL("../background.js", import.meta.url),
    "utf8"
  );

  vm.runInNewContext(source, {
    chrome: harness.chrome,
    console,
    Date,
    Object,
    Promise,
    Set,
    clearTimeout: harness.clearTimeout,
    setTimeout: harness.setTimeout,
    ChatterinoProtocol: {
      CURRENT_VERSION: 1,
      normalizeOutbound: (message) => message,
    },
    browser: undefined,
  });

  await harness.listeners["tabs.onActivated"]({ tabId: 7 });

  assert.equal(
    JSON.stringify(harness.sentMessages),
    JSON.stringify([[7, { action: "requestChatRect" }]])
  );
});

test("native readiness asks the active Twitch tab for fresh chat geometry", async () => {
  const harness = createBackgroundHarness();
  const source = await readFile(
    new URL("../background.js", import.meta.url),
    "utf8"
  );

  vm.runInNewContext(source, {
    chrome: harness.chrome,
    console,
    Date,
    Object,
    Promise,
    Set,
    clearTimeout: harness.clearTimeout,
    setTimeout: harness.setTimeout,
    ChatterinoProtocol: {
      CURRENT_VERSION: 1,
      normalizeOutbound: (message) => message,
    },
    browser: undefined,
  });

  harness.listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 0, y: 0, width: 340, height: 720 },
      dpr: 1,
    },
    {
      tab: {
        id: 7,
        windowId: 42,
        url: "https://www.twitch.tv/example",
        highlighted: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  for (const status of ["native-host-ready", "desktop-ready"]) {
    harness.sentMessages.length = 0;
    harness.listeners["native.onMessage"]({ type: "status", status });
    await new Promise((resolve) => setTimeout(resolve, 0));

    assert.equal(
      JSON.stringify(harness.sentMessages),
      JSON.stringify([[7, { action: "requestChatRect" }]])
    );
  }
});

test("native readiness attaches after Chatterino steals focus from Edge", async () => {
  const harness = createBackgroundHarness({ windowFocused: false });
  const source = await readFile(
    new URL("../background.js", import.meta.url),
    "utf8"
  );

  vm.runInNewContext(source, {
    chrome: harness.chrome,
    console,
    Date,
    Object,
    Promise,
    Set,
    clearTimeout: harness.clearTimeout,
    setTimeout: harness.setTimeout,
    ChatterinoProtocol: {
      CURRENT_VERSION: 1,
      normalizeOutbound: (message) => message,
    },
    browser: undefined,
  });

  await new Promise((resolve) => setTimeout(resolve, 0));

  harness.sentMessages.length = 0;
  harness.nativeMessages.length = 0;
  harness.listeners["native.onMessage"]({
    type: "status",
    status: "desktop-ready",
  });
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(
    JSON.stringify(harness.sentMessages),
    JSON.stringify([[7, { action: "requestChatRect" }]])
  );

  harness.listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 100, y: 50, width: 340, height: 720 },
      dpr: 1,
    },
    {
      tab: {
        id: 7,
        windowId: 42,
        url: "https://www.twitch.tv/example",
        highlighted: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 1);
  const attachRequestId = harness.nativeMessages[0].attachRequestId;
  assert.equal(typeof attachRequestId, "string");
  assert.deepEqual(JSON.parse(JSON.stringify(harness.nativeMessages[0])), {
    action: "select",
    attach: true,
    attachRequestId,
    browserWindowFocused: false,
    name: "example",
    size: { height: 720, pixelRatio: 1, width: 340, x: 100 },
    startupReplay: true,
    type: "twitch",
    version: 0,
    winId: "42",
  });

  harness.listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 100, y: 50, width: 340, height: 720 },
      dpr: 1,
    },
    {
      tab: {
        id: 7,
        windowId: 42,
        url: "https://www.twitch.tv/example",
        highlighted: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 1);
});

test("attach retries continue until the desktop acknowledges the native window", async () => {
  const harness = createBackgroundHarness();
  const source = await readFile(
    new URL("../background.js", import.meta.url),
    "utf8"
  );

  vm.runInNewContext(source, {
    chrome: harness.chrome,
    console,
    Date,
    Object,
    Promise,
    Set,
    clearTimeout: harness.clearTimeout,
    setTimeout: harness.setTimeout,
    ChatterinoProtocol: {
      CURRENT_VERSION: 1,
      normalizeOutbound: (message) => message,
    },
    browser: undefined,
  });
  await new Promise((resolve) => setTimeout(resolve, 0));

  harness.nativeMessages.length = 0;
  harness.listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 100, y: 50, width: 340, height: 720 },
      dpr: 1,
    },
    {
      tab: {
        id: 7,
        windowId: 42,
        url: "https://www.twitch.tv/example",
        highlighted: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 1);
  assert.equal(harness.timers.length, 1);

  const firstAttempt = harness.nativeMessages[0];
  assert.equal(firstAttempt.browserWindowFocused, true);
  assert.equal(firstAttempt.startupReplay, false);

  harness.timers[0].callback();
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 2);
  assert.equal(
    harness.nativeMessages[1].attachRequestId,
    firstAttempt.attachRequestId
  );
  assert.equal(harness.nativeMessages[1].browserWindowFocused, false);
  assert.equal(harness.nativeMessages[1].startupReplay, true);

  harness.listeners["native.onMessage"]({
    type: "status",
    status: "chat-attached",
    winId: "42",
    attachRequestId: firstAttempt.attachRequestId,
  });

  assert.equal(harness.timers.length, 2);
  assert.equal(harness.timers[1].cleared, true);
  harness.timers[1].callback();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(harness.nativeMessages.length, 2);
});

test("native host preserves Edge's startup HWND for a delayed geometry replay", () => {
  assert.match(browserHostSource, /quintptr startupBrowserWindow = 0/);
  assert.match(
    browserHostSource,
    /isSupportedBrowserWindow\(foreground\)[\s\S]*startupBrowserWindow/
  );
  assert.match(
    browserHostSource,
    /target == 0 && root\.value\("startupReplay"\)\.toBool\(\)[\s\S]*startupBrowserWindow/
  );
});
