import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const overlaySource = await readFile(
  new URL("../overlay.js", import.meta.url),
  "utf8"
);
const backgroundSource = await readFile(
  new URL("../background.js", import.meta.url),
  "utf8"
);
const stylesSource = await readFile(
  new URL("../styles.css", import.meta.url),
  "utf8"
);

function createStyleBag() {
  const props = new Map();
  return {
    setProperty(name, value, priority = "") {
      props.set(name, { value, priority });
    },
    getPropertyValue(name) {
      return props.get(name)?.value ?? "";
    },
  };
}

function runOverlayHarness({
  rect = { x: 1000, y: 50, width: 340, height: 800 },
  innerHeight = 900,
  promoBanners = [],
  stickyFooters = [],
} = {}) {
  const sentMessages = [];
  const runtimeListeners = [];
  const mutationCallbacks = [];
  const windowListeners = new Map();
  const shell = {
    children: [{}],
    getBoundingClientRect: () => ({ ...rect }),
  };

  const documentMock = {
    fullscreenElement: null,
    body: { childElementCount: 2 },
    documentElement: {
      classList: {
        add() {},
        remove() {},
        contains: () => false,
      },
      setAttribute() {},
      removeAttribute() {},
    },
    getElementsByClassName(name) {
      return name === "chat-shell" ? [shell] : [];
    },
    querySelectorAll(selector) {
      const results = [];
      for (const banner of promoBanners) {
        if (
          selector === 'img[src*="gta6-bottom-banner"]' &&
          banner.img
        ) {
          results.push(banner.img);
        }
        if (
          selector ===
            '[class*="banner--"] a[href="/directory/category/grand-theft-auto-vi"]' &&
          banner.link
        ) {
          results.push(banner.link);
        }
      }
      if (selector === "#twilight-sticky-footer-root") {
        results.push(...stickyFooters);
      }
      return results;
    },
    addEventListener() {},
  };

  const location = {
    href: "https://www.twitch.tv/example",
    pathname: "/example",
  };
  const windowMock = {
    location,
    devicePixelRatio: 1,
    innerHeight,
    addEventListener(event, handler) {
      if (!windowListeners.has(event)) windowListeners.set(event, []);
      windowListeners.get(event).push(handler);
    },
    dispatchEvent() {},
  };

  const chromeMock = {
    runtime: {
      id: "test-extension",
      sendMessage(message, callback) {
        sentMessages.push(message);
        if (message?.type === "get-setting" && callback) {
          callback(true);
        } else if (message?.type === "get-os" && callback) {
          callback("win");
        }
      },
      onMessage: {
        addListener(fn) {
          runtimeListeners.push(fn);
        },
      },
    },
    storage: {
      onChanged: { addListener() {} },
    },
  };

  class MockMutationObserver {
    constructor(cb) {
      mutationCallbacks.push(cb);
    }
    observe() {}
    disconnect() {}
  }

  const scheduled = [];
  const context = {
    window: windowMock,
    document: documentMock,
    location,
    chrome: chromeMock,
    console,
    Date,
    Math,
    Number,
    Set,
    WeakSet,
    CustomEvent: class CustomEvent {
      constructor(type, init) {
        this.type = type;
        this.detail = init?.detail;
      }
    },
    MutationObserver: MockMutationObserver,
    setTimeout(fn, delay) {
      scheduled.push({ fn, delay });
      return scheduled.length;
    },
    clearTimeout() {},
    setInterval() {
      return 1;
    },
    clearInterval() {},
  };
  context.globalThis = context;

  vm.runInNewContext(overlaySource, context);

  return {
    sentMessages,
    runtimeListeners,
    mutationCallbacks,
    scheduled,
    shell,
    setRect(nextRect) {
      rect = nextRect;
    },
  };
}

test("overlay.js reports bottomOffset when .chat-shell stops above the viewport bottom", () => {
  const harness = runOverlayHarness({
    rect: { x: 1000, y: 50, width: 340, height: 790 },
    innerHeight: 900,
  });

  const resizeMessages = harness.sentMessages.filter(
    (msg) => msg?.type === "chat-resized"
  );
  assert.ok(resizeMessages.length >= 1, "expected at least one chat-resized message");
  assert.equal(resizeMessages.at(-1).bottomOffset, 60);
});

test("overlay.js hides and dismisses Twitch's GTA VI bottom banner once and re-measures chat", () => {
  let closeClicks = 0;
  const wrapperStyle = createStyleBag();
  const bannerStyle = createStyleBag();
  const wrapper = {
    childElementCount: 1,
    style: wrapperStyle,
  };
  const closeButton = {
    click() {
      closeClicks += 1;
    },
  };
  const banner = {
    parentElement: wrapper,
    style: bannerStyle,
    querySelector(sel) {
      return sel === 'button[aria-label="Close"]' ? closeButton : null;
    },
  };
  const img = {
    closest(sel) {
      return sel === '[class*="banner--"]' ? banner : null;
    },
  };
  const link = {
    closest(sel) {
      return sel === '[class*="banner--"]' ? banner : null;
    },
  };

  const harness = runOverlayHarness({
    rect: { x: 1000, y: 50, width: 340, height: 850 },
    innerHeight: 900,
    promoBanners: [{ img, link, banner }],
  });

  assert.equal(wrapperStyle.getPropertyValue("display"), "none");
  assert.equal(bannerStyle.getPropertyValue("display"), "none");
  assert.equal(closeClicks, 1);

  // Subsequent DOM mutations must not click Close repeatedly.
  for (const cb of harness.mutationCallbacks) cb();
  for (const item of harness.scheduled.splice(0)) item.fn();
  assert.equal(closeClicks, 1);
});

test("styles.css hides the GTA VI promotional bottom banner wrapper and inner banner", () => {
  assert.match(stylesSource, /gta6-bottom-banner/);
  assert.match(stylesSource, /\/directory\/category\/grand-theft-auto-vi/);
  assert.match(stylesSource, /#twilight-sticky-footer-root/);
  assert.match(stylesSource, /upsell-bottom-banner/);
});

test("overlay.js hides Twitch's logged-out Sign Up sticky footer without touching its parent or unrelated footers", () => {
  const parentStyle = createStyleBag();
  const upsellFooterStyle = createStyleBag();
  const otherFooterStyle = createStyleBag();
  const parent = { childElementCount: 1, style: parentStyle };

  const upsellFooter = {
    id: "twilight-sticky-footer-root",
    parentElement: parent,
    style: upsellFooterStyle,
    querySelector(sel) {
      if (sel.includes("upsell-bottom-banner")) {
        return { className: "upsell-bottom-banner__coolcat" };
      }
      return null;
    },
  };
  const otherFooter = {
    id: "twilight-sticky-footer-root",
    parentElement: parent,
    style: otherFooterStyle,
    querySelector() {
      return null;
    },
  };

  runOverlayHarness({
    rect: { x: 1000, y: 50, width: 340, height: 850 },
    innerHeight: 900,
    stickyFooters: [upsellFooter, otherFooter],
  });

  assert.equal(upsellFooterStyle.getPropertyValue("display"), "none");
  assert.equal(parentStyle.getPropertyValue("display"), "");
  assert.equal(otherFooterStyle.getPropertyValue("display"), "");
});

test("background.js scales and forwards bottomOffset and rejects negative bottomOffset", async () => {
  const listeners = {};
  const nativeMessages = [];
  const activeTab = {
    id: 7,
    windowId: 42,
    url: "https://www.twitch.tv/example",
    highlighted: true,
    active: true,
  };
  const createEvent = (name) => ({
    addListener(fn) {
      listeners[name] = fn;
    },
  });

  vm.runInNewContext(backgroundSource, {
    chrome: {
      windows: {
        WINDOW_ID_NONE: -1,
        get: async () => ({ focused: true, state: "normal" }),
        onRemoved: createEvent("windows.onRemoved"),
        onFocusChanged: createEvent("windows.onFocusChanged"),
      },
      tabs: {
        get: async () => activeTab,
        query: async () => [activeTab],
        getZoom: async () => 1.25,
        sendMessage: async () => {},
        onActivated: createEvent("tabs.onActivated"),
        onUpdated: createEvent("tabs.onUpdated"),
        onDetached: createEvent("tabs.onDetached"),
        onCreated: createEvent("tabs.onCreated"),
        onRemoved: createEvent("tabs.onRemoved"),
      },
      storage: {
        session: { get: async () => ({}), set: async () => {} },
        local: { get: async () => ({}), set: async () => {} },
      },
      runtime: {
        id: "own-extension",
        connectNative: () => ({
          onMessage: createEvent("native.onMessage"),
          onDisconnect: createEvent("native.onDisconnect"),
          postMessage(msg) {
            nativeMessages.push(msg);
          },
        }),
        sendNativeMessage() {},
        onMessage: createEvent("runtime.onMessage"),
        getPlatformInfo: (cb) => (cb ? cb({ os: "win" }) : Promise.resolve({ os: "win" })),
        lastError: null,
      },
      action: { setBadgeText: async () => {} },
    },
    console,
    Date,
    Object,
    Promise,
    Set,
    Map,
    Math,
    Number,
    setTimeout: () => 1,
    clearTimeout() {},
    ChatterinoProtocol: {
      CURRENT_VERSION: 1,
      normalizeOutbound: (msg) => msg,
    },
    crypto: globalThis.crypto,
    browser: undefined,
  });

  await new Promise((r) => setTimeout(r, 10));
  const initialCount = nativeMessages.length;

  // Negative bottomOffset must be rejected.
  listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 100, y: 50, width: 340, height: 700 },
      dpr: 1.25,
      bottomOffset: -10,
    },
    { id: "own-extension", tab: activeTab },
    () => {}
  );
  await new Promise((r) => setTimeout(r, 20));
  assert.equal(nativeMessages.length, initialCount);

  // Positive bottomOffset is scaled by zoom (1.25) and forwarded.
  listeners["runtime.onMessage"](
    {
      type: "chat-resized",
      rect: { x: 100, y: 50, width: 340, height: 700 },
      dpr: 1.25,
      bottomOffset: 60,
    },
    { id: "own-extension", tab: activeTab },
    () => {}
  );
  await new Promise((r) => setTimeout(r, 20));

  const selects = nativeMessages.filter((m) => m.action === "select");
  assert.equal(selects.length, 1);
  assert.equal(selects[0].size.bottomOffset, 75);
});
