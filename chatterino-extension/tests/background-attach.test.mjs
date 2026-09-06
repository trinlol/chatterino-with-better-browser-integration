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
  const sessionStorage = {};
  let nextTimerId = 1;
  const activeTab = {
    id: 7,
    windowId: 42,
    url: "https://www.twitch.tv/example",
    highlighted: true,
        active: true,
  };

  return {
    listeners,
    sentMessages,
    nativeMessages,
    timers,
    sessionStorage,
    activeTab,
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
        session: {
          get: async (key) =>
            typeof key === "string" ? { [key]: sessionStorage[key] } : {},
          set: async (value) => Object.assign(sessionStorage, value),
        },
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
    crypto: globalThis.crypto,
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
    crypto: globalThis.crypto,
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
        active: true,
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

test("native readiness never attaches to an unfocused arbitrary window", async () => {
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
    crypto: globalThis.crypto,
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
        active: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 0);

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
        active: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(harness.nativeMessages.length, 0);
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
    crypto: globalThis.crypto,
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
        active: true,
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

  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(harness.timers.length, 2);
  assert.equal(harness.timers[1].cleared, true);
  harness.timers[1].callback();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(harness.nativeMessages.length, 2);
});

test("protocol v2 uses one correlation ID and accepts the native acknowledgement", async () => {
  const harness = createBackgroundHarness();
  const context = {
    chrome: harness.chrome,
    console,
    Date,
    Object,
    Promise,
    Set,
    clearTimeout: harness.clearTimeout,
    setTimeout: harness.setTimeout,
    ChatterinoProtocol: {
      CURRENT_VERSION: 2,
      normalizeOutbound: (message) => message,
    },
    crypto: globalThis.crypto,
    browser: undefined,
  };
  vm.runInNewContext(
    await readFile(new URL("../extension-lifecycle.js", import.meta.url), "utf8"),
    context
  );
  vm.runInNewContext(
    await readFile(new URL("../background.js", import.meta.url), "utf8"),
    context
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  harness.listeners["native.onMessage"]({
    type: "status",
    status: "native-host-ready",
    protocolVersion: 2,
    capabilities: ["sessions"],
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
        active: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  const select = harness.nativeMessages.find(
    (message) => message.action === "select"
  );
  assert.ok(select);
  assert.equal(select.protocolVersion, 2);
  assert.equal(select.requestId, select.attachRequestId);

  harness.listeners["native.onMessage"]({
    type: "status",
    status: "chat-attached",
    protocolVersion: 2,
    requestId: select.requestId,
    attachRequestId: select.requestId,
    sessionId: select.sessionId,
    browserWindowId: select.browserWindowId,
    tabId: select.tabId,
    generation: select.generation,
    winId: select.winId,
    leaseExpiresAt: select.leaseExpiresAt,
  });
  await new Promise((resolve) => setTimeout(resolve, 0));

  const attachedState = harness.sentMessages.find(
    ([, message]) =>
      message.action === "nativeAttachState" && message.state === "attached"
  )?.[1];
  assert.ok(attachedState);
  assert.equal(attachedState.sessionId, select.sessionId);
  assert.equal(attachedState.generation, select.generation);
  assert.equal(attachedState.leaseExpiresAt, select.leaseExpiresAt);
});

test("legacy native acknowledgement does not create an expiring v2 lease", async () => {
  const harness = createBackgroundHarness();
  const source = await readFile(
    new URL("../background.js", import.meta.url),
    "utf8"
  );
  const context = {
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
    crypto: globalThis.crypto,
    browser: undefined,
  };
  vm.runInNewContext(
    await readFile(new URL("../extension-lifecycle.js", import.meta.url), "utf8"),
    context
  );
  vm.runInNewContext(source, context);
  await new Promise((resolve) => setTimeout(resolve, 0));

  harness.listeners["native.onMessage"]({
    type: "status",
    status: "desktop-ready",
  });
  await new Promise((resolve) => setTimeout(resolve, 0));

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
        active: true,
      },
    },
    () => {}
  );
  await new Promise((resolve) => setTimeout(resolve, 0));

  const attach = harness.nativeMessages.find(
    (message) => message.action === "select"
  );
  harness.listeners["native.onMessage"]({
    type: "status",
    status: "chat-attached",
    winId: "42",
    attachRequestId: attach.attachRequestId,
  });
  await new Promise((resolve) => setTimeout(resolve, 0));

  const attachedState = harness.sentMessages.find(
    ([, message]) =>
      message.action === "nativeAttachState" && message.state === "attached"
  )?.[1];
  assert.ok(attachedState);
  assert.equal(attachedState.leaseExpiresAt, 0);
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

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));
async function loadTabLifecycleHarness(options) {
  const harness = createBackgroundHarness(options);
  const context = vm.createContext({
    chrome: harness.chrome, console: { log() {}, info() {}, warn() {}, error() {} },
    Date, Object, Promise, Set, crypto: globalThis.crypto,
    clearTimeout: harness.clearTimeout, setTimeout: harness.setTimeout,
    ChatterinoProtocol: { CURRENT_VERSION: 2, normalizeOutbound: (message) => message },
    browser: undefined,
  });
  for (const file of ['extension-lifecycle.js', 'background.js']) {
    vm.runInContext(await readFile(new URL(`../${file}`, import.meta.url), 'utf8'), context);
  }
  await settle();
  harness.listeners['native.onMessage']({type: 'status', status: 'desktop-ready', protocolVersion: 2});
  await settle();
  harness.nativeMessages.length = 0;
  harness.sentMessages.length = 0;
  harness.measure = (tab = {...harness.activeTab}, x = 1200) => harness.listeners['runtime.onMessage'](
    {type: 'chat-resized', rect: {x, y: 50, width: 306, height: 720}, dpr: 1}, {tab}, () => {});
  harness.ack = (select) => harness.listeners['native.onMessage']({
    ...select, type: 'status', status: 'chat-attached', attachRequestId: select.requestId,
  });
  return harness;
}

test('switching to an unreadable non-Twitch tab detaches without OS focus', async () => {
  const h = await loadTabLifecycleHarness();
  h.measure(); await settle();
  const select = h.nativeMessages.find(m => m.action === 'select');
  assert.ok(select);
  h.ack(select); await settle();
  h.chrome.windows.get = async () => ({focused: false, state: 'normal'});
  h.activeTab.id = 8; delete h.activeTab.url;
  h.sentMessages.length = 0;
  await h.listeners['tabs.onActivated']({tabId: 8, windowId: 42});
  await settle();
  assert.ok(h.nativeMessages.some(m => m.action === 'detach' && m.sessionId === select.sessionId));
  assert.equal(h.sessionStorage.desiredSessions[select.sessionId].desired, false);
  h.ack(select); await settle();
  assert.equal(h.sentMessages.some(([,m]) => m.state === 'attached'), false);
  for (const timer of [...h.timers]) timer.callback();
  await settle();
  assert.equal(h.nativeMessages.filter(m => m.action === 'select').length, 1);
});

test('v2 detach carries the session channel so the desktop accepts it', async () => {
  const h = await loadTabLifecycleHarness();
  h.measure(); await settle();
  const select = h.nativeMessages.find(m => m.action === 'select');
  assert.ok(select);
  h.ack(select); await settle();
  h.chrome.windows.get = async () => ({focused: false, state: 'normal'});
  h.activeTab.id = 8; delete h.activeTab.url;
  h.nativeMessages.length = 0;
  await h.listeners['tabs.onActivated']({tabId: 8, windowId: 42});
  await settle();
  const detach = h.nativeMessages.find(m => m.action === 'detach');
  assert.ok(detach, 'expected a native detach message');
  // The desktop v2 parser requires a complete attachment identity; without a
  // non-empty channel every detach is rejected as malformed-message and the
  // overlay stays pinned over unrelated tabs.
  assert.equal(detach.protocolVersion, 2);
  assert.ok(detach.channel, 'detach must include a non-empty channel');
  assert.equal(detach.channel, 'example');
  assert.equal(detach.sessionId, select.sessionId);
});

test('a resize awaiting zoom cannot reattach after a tab switch', async () => {
  const h = await loadTabLifecycleHarness();
  let releaseZoom;
  h.chrome.tabs.getZoom = () => new Promise(resolve => { releaseZoom = resolve; });
  h.measure(); await settle();
  assert.ok(releaseZoom);
  h.activeTab.id = 8; delete h.activeTab.url;
  await h.listeners['tabs.onActivated']({tabId: 8, windowId: 42});
  releaseZoom(1); await settle();
  assert.equal(h.nativeMessages.some(m => m.action === 'select'), false);
  assert.equal(Object.values(h.sessionStorage.desiredSessions).some(s => s.desired), false);
});

test('a highlighted but inactive Twitch tab cannot attach or detach the visible tab', async () => {
  const h = await loadTabLifecycleHarness();
  h.measure(); await settle();
  const select = h.nativeMessages.find(m => m.action === 'select');
  h.ack(select); await settle();
  const inactive = {...h.activeTab, id: 9, active: false, highlighted: true};
  h.chrome.tabs.get = async (id) => id === 9 ? inactive : h.activeTab;
  h.nativeMessages.length = 0;
  h.measure(inactive);
  h.listeners['runtime.onMessage']({type: 'detach'}, {tab: inactive}, () => {});
  await settle();
  assert.equal(h.nativeMessages.length, 0);
});

test('returning to Twitch attaches again with fresh geometry', async () => {
  const h = await loadTabLifecycleHarness();
  h.measure(); await settle();
  const first = h.nativeMessages.find(m => m.action === 'select');
  h.ack(first); await settle();
  const twitch = {...h.activeTab};
  h.activeTab.id = 8; delete h.activeTab.url;
  await h.listeners['tabs.onActivated']({tabId: 8, windowId: 42});
  Object.assign(h.activeTab, twitch);
  await h.listeners['tabs.onActivated']({tabId: 7, windowId: 42});
  h.measure(undefined, 850); await settle();
  const last = h.nativeMessages.filter(m => m.action === 'select').at(-1);
  assert.equal(last.size.x, 850);
  assert.notEqual(last.requestId, first.requestId);
  assert.ok(last.generation > first.generation);
  h.ack(last); await settle();
  assert.equal(h.sessionStorage.desiredSessions[last.sessionId].attached, true);
});

test('real overlay and background do not feed detach acknowledgements back forever', async () => {
  const h = await loadTabLifecycleHarness();
  const pending = [];
  const windowEvents = {};
  const classes = new Set();
  const document = {
    fullscreenElement: null,
    documentElement: {classList: {add: k => classes.add(k), remove: k => classes.delete(k)}, setAttribute() {}, removeAttribute() {}},
    addEventListener() {},
    getElementsByClassName: () => [{children: [{}], getBoundingClientRect: () => ({x: 1200, y: 50, width: 306, height: 720})}],
  };
  const location = {href: h.activeTab.url, pathname: '/example'};
  const overlayContext = vm.createContext({
    console: {log() {}, error() {}}, document, location,
    window: {location, devicePixelRatio: 1, addEventListener: (event, handler) => {windowEvents[event] = handler;}, dispatchEvent() {}},
    chrome: {
      runtime: {
        sendMessage: (message, callback) => {
          if (message.type === 'get-setting') return callback(true);
          if (message.type === 'get-os') return callback('win');
          pending.push(message);
        },
        onMessage: {addListener: fn => { h.deliverOverlay = fn; }},
      },
      storage: {onChanged: {addListener() {}}},
    },
    MutationObserver: class {observe() {}}, CustomEvent: class {},
    setTimeout: h.setTimeout, clearTimeout: h.clearTimeout, setInterval() {},
  });
  for (const file of ['extension-lifecycle.js', 'overlay.js']) {
    vm.runInContext(await readFile(new URL(`../${file}`, import.meta.url), 'utf8'), overlayContext);
  }
  h.chrome.tabs.sendMessage = async (tabId, message) => {
    h.sentMessages.push([tabId, message]);
    h.deliverOverlay(message);
  };
  async function drain() {
    for (let i = 0; i < 12; i++) {
      const batch = pending.splice(0);
      for (const m of batch) h.listeners['runtime.onMessage'](m, {tab: {...h.activeTab}}, () => {});
      await settle();
      if (!pending.length) return;
    }
    assert.fail('overlay/background messaging failed to settle');
  }
  await drain();
  const select = h.nativeMessages.find(m => m.action === 'select');
  h.ack(select); await settle();
  assert.equal(classes.has('chatterino-companion-active'), true);
  document.fullscreenElement = {};
  windowEvents.resize();
  await drain();
  assert.equal(classes.has('chatterino-companion-active'), false);
  assert.equal(h.nativeMessages.filter(m => m.action === 'detach').length, 1);
  windowEvents.resize(); await drain();
  assert.equal(h.nativeMessages.filter(m => m.action === 'detach').length, 1);
  document.fullscreenElement = null;
  windowEvents.resize(); await drain();
  assert.equal(h.nativeMessages.filter(m => m.action === 'select').length, 2);
});
