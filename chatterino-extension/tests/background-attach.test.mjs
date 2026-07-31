import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

function createEvent(listeners, name) {
  return {
    addListener(listener) {
      listeners[name] = listener;
    },
  };
}

function createBackgroundHarness() {
  const listeners = {};
  const sentMessages = [];
  const activeTab = {
    id: 7,
    windowId: 42,
    url: "https://www.twitch.tv/example",
    highlighted: true,
  };

  return {
    listeners,
    sentMessages,
    chrome: {
      windows: {
        WINDOW_ID_NONE: -1,
        get: async () => ({ focused: true, state: "normal" }),
        onRemoved: createEvent(listeners, "windows.onRemoved"),
        onFocusChanged: createEvent(listeners, "windows.onFocusChanged"),
      },
      tabs: {
        get: async () => activeTab,
        query: async (queryInfo) =>
          queryInfo.active && queryInfo.currentWindow ? [activeTab] : [],
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
          postMessage() {},
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
