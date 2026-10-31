import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

// Minimal fake DOM, just enough for content.js to run its sync loop with no
// Twitch controls mounted. Timers are driven manually.

class FakeClassList {
  constructor() {
    this.set = new Set();
  }
  add(...names) {
    names.forEach((n) => this.set.add(n));
  }
  remove(...names) {
    names.forEach((n) => this.set.delete(n));
  }
  toggle(name, force) {
    const on = force === undefined ? !this.set.has(name) : Boolean(force);
    if (on) this.set.add(name);
    else this.set.delete(name);
    return on;
  }
  contains(name) {
    return this.set.has(name);
  }
}

class FakeElement {
  constructor(doc, tagName = "div", className = "") {
    this.ownerDocument = doc;
    this.tagName = tagName.toUpperCase();
    this.nodeType = 1;
    this.children = [];
    this.parentElement = null;
    this.classList = new FakeClassList();
    className.split(/\s+/).filter(Boolean).forEach((c) => this.classList.add(c));
    this.attributes = {};
    this.dataset = {};
    this.textContent = "";
    this.innerText = "";
    this.listeners = {};
    const store = {};
    this.style = {
      setProperty: (k, v) => (store[k] = v),
      removeProperty: (k) => delete store[k],
      getPropertyValue: (k) => store[k] ?? "",
      order: "",
    };
    this._id = "";
  }
  get id() {
    return this._id;
  }
  set id(value) {
    this._id = value;
    this.ownerDocument.ids.set(value, this);
  }
  get isConnected() {
    let node = this;
    while (node) {
      if (node === this.ownerDocument.documentElement) return true;
      node = node.parentElement;
    }
    return false;
  }
  get offsetWidth() {
    return 0;
  }
  get offsetHeight() {
    return 0;
  }
  set innerHTML(_v) {}
  appendChild(child) {
    child.parentElement?.removeChild(child);
    child.parentElement = this;
    this.children.push(child);
    return child;
  }
  removeChild(child) {
    this.children = this.children.filter((c) => c !== child);
    child.parentElement = null;
    return child;
  }
  remove() {
    this.parentElement?.removeChild(this);
  }
  replaceChildren(...nodes) {
    this.children = [];
    nodes.forEach((n) => this.appendChild(n));
  }
  contains(node) {
    while (node) {
      if (node === this) return true;
      node = node.parentElement;
    }
    return false;
  }
  setAttribute(k, v) {
    this.attributes[k] = String(v);
  }
  getAttribute(k) {
    return this.attributes[k] ?? null;
  }
  hasAttribute(k) {
    return k in this.attributes;
  }
  removeAttribute(k) {
    delete this.attributes[k];
  }
  addEventListener(type, fn) {
    (this.listeners[type] ||= []).push(fn);
  }
  matchesSelector(selector) {
    return selector
      .split(",")
      .map((s) => s.trim())
      .some((s) => s.startsWith(".") && this.classList.contains(s.slice(1)));
  }
  matches(selector) {
    return this.matchesSelector(selector);
  }
  closest(selector) {
    let node = this;
    while (node) {
      if (node.matchesSelector?.(selector)) return node;
      node = node.parentElement;
    }
    return null;
  }
  querySelector() {
    return null;
  }
  querySelectorAll() {
    return [];
  }
  getBoundingClientRect() {
    return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 };
  }
}

function createHarness({ path = "/somechannel" } = {}) {
  const timers = new Map();
  let nextTimer = 1;
  // content.js throttles syncs with Date.now(), so the clock must follow the
  // fake timers.
  let clockNow = 1_000_000;
  class FakeDate extends Date {
    static now() {
      return clockNow;
    }
  }
  const addTimer = (fn, delay, repeat) => {
    const id = nextTimer++;
    timers.set(id, { fn, delay: Number(delay) || 0, interval: Math.max(1, Number(delay) || 0), repeat });
    return id;
  };
  const clearTimer = (id) => timers.delete(id);

  const doc = { ids: new Map(), listeners: {} };
  doc.documentElement = new FakeElement(doc, "html");
  doc.body = new FakeElement(doc, "body");
  doc.documentElement.appendChild(doc.body);
  doc.createElement = (tag) => new FakeElement(doc, tag);
  doc.getElementById = (id) => {
    const el = doc.ids.get(id);
    return el?.isConnected && el.id === id ? el : null;
  };
  doc.querySelector = (selector) => {
    const wanted = selector.trim();
    const visit = (node) => {
      for (const child of node.children) {
        if (wanted.startsWith(".") && !wanted.includes(" ") && child.classList.contains(wanted.slice(1))) {
          return child;
        }
        const found = visit(child);
        if (found) return found;
      }
      return null;
    };
    return visit(doc.documentElement);
  };
  doc.querySelectorAll = () => [];
  doc.addEventListener = (type, fn) => (doc.listeners[type] ||= []).push(fn);
  doc.createTreeWalker = () => ({ nextNode: () => null });
  doc.visibilityState = "visible";

  const observers = [];
  class FakeMutationObserver {
    constructor(callback) {
      this.callback = callback;
      this.targets = [];
      observers.push(this);
    }
    observe(target) {
      this.targets.push(target);
    }
    disconnect() {
      this.targets = [];
    }
    takeRecords() {
      return [];
    }
  }

  const windowListeners = {};
  const sent = [];
  const runtime = {
    id: "extension-id",
    sendMessage: (message) => {
      if (!runtime.id) throw new Error("Extension context invalidated.");
      sent.push(message);
    },
    onMessage: { addListener() {} },
  };

  const context = {
    console: process.env.DEBUG_CC ? console : { log() {}, warn() {}, error() {} },
    document: doc,
    location: { pathname: path },
    Node: { TEXT_NODE: 3, ELEMENT_NODE: 1 },
    NodeFilter: { SHOW_TEXT: 4 },
    MutationObserver: FakeMutationObserver,
    CustomEvent: class {
      constructor(type, init) {
        this.type = type;
        this.detail = init?.detail;
      }
    },
    Event: class {},
    KeyboardEvent: class {},
    MouseEvent: class {},
    InputEvent: class {},
    HTMLTextAreaElement: class {},
    HTMLInputElement: class {},
    getComputedStyle: () => ({ position: "static", overflowY: "visible" }),
    requestAnimationFrame: (fn) => addTimer(fn, 16, false),
    cancelAnimationFrame: clearTimer,
    setTimeout: (fn, delay) => addTimer(fn, delay, false),
    clearTimeout: clearTimer,
    setInterval: (fn, delay) => addTimer(fn, delay, true),
    clearInterval: clearTimer,
    innerWidth: 1920,
    innerHeight: 1080,
    performance: { now: () => clockNow },
    addEventListener: (type, fn) => (windowListeners[type] ||= []).push(fn),
    removeEventListener: (type, fn) => {
      windowListeners[type] = (windowListeners[type] || []).filter((l) => l !== fn);
    },
    dispatchEvent: (event) => {
      (windowListeners[event.type] || []).forEach((fn) => fn(event));
      return true;
    },
    chrome: {
      runtime,
      storage: {
        local: { get: (_d, cb) => cb({ autoClaimEnabled: true }) },
        onChanged: { addListener() {} },
      },
    },
    structuredClone,
    Date: FakeDate,
    JSON,
    Math,
  };
  context.window = context;
  context.globalThis = context;
  vm.createContext(context);

  // Run every timer that is due within `ms`, including ones they schedule.
  function advance(ms = 0) {
    let budget = 10000;
    let elapsed = 0;
    const order = () =>
      [...timers.entries()].sort((a, b) => a[1].delay - b[1].delay);
    while (budget-- > 0) {
      const next = order().find(([, t]) => t.delay <= ms - elapsed);
      if (!next) break;
      const [id, timer] = next;
      const step = timer.delay;
      elapsed += step;
      clockNow += step;
      for (const t of timers.values()) t.delay -= step;
      if (timer.repeat) {
        timer.delay = timer.interval;
      } else {
        timers.delete(id);
      }
      timer.fn();
    }
    clockNow += ms - elapsed;
    for (const t of timers.values()) t.delay = Math.max(0, t.delay - (ms - elapsed));
  }

  return { context, doc, observers, timers, sent, runtime, windowListeners, advance };
}

async function loadContentScript(harness) {
  for (const file of [
    "protocol.js",
    "page-data.js",
    "activity-state.js",
    "voting-ui.js",
    "content.js",
  ]) {
    const source = await readFile(new URL(`../${file}`, import.meta.url), "utf8");
    vm.runInContext(source, harness.context, { filename: file });
  }
  // The sync observer is the first one content.js creates.
  return harness.observers[0];
}

test("sync observer stays attached after a sync and follows a replaced chat shell", async () => {
  const h = createHarness();
  const shell = new FakeElement(h.doc, "div", "chat-shell");
  h.doc.body.appendChild(shell);

  const observer = await loadContentScript(h);
  assert.equal(observer.targets.length, 1);
  assert.equal(observer.targets[0], shell);

  h.advance(0); // initial scheduleSync -> runSync
  assert.equal(
    observer.targets.length,
    1,
    "runSync disconnected the observer and never re-attached it"
  );
  assert.equal(observer.targets[0], shell);

  // SPA navigation replaces the shell.
  shell.remove();
  const nextShell = new FakeElement(h.doc, "div", "chat-shell");
  h.doc.body.appendChild(nextShell);
  h.advance(2000); // safety poll -> runSync
  assert.equal(observer.targets.length, 1);
  assert.equal(observer.targets[0], nextShell);
});

test("chat line churn does not schedule a sync, other chat mutations do", async () => {
  const h = createHarness();
  const shell = new FakeElement(h.doc, "div", "chat-shell");
  const lines = new FakeElement(h.doc, "div", "chat-scrollable-area__message-container");
  const header = new FakeElement(h.doc, "div", "chat-room__header");
  shell.appendChild(lines);
  shell.appendChild(header);
  h.doc.body.appendChild(shell);
  const observer = await loadContentScript(h);
  h.advance(300);

  const pendingBefore = h.timers.size;
  observer.callback([{ type: "childList", target: lines }]);
  assert.equal(h.timers.size, pendingBefore, "chat line mutation scheduled a sync");

  observer.callback([{ type: "childList", target: lines }, { type: "childList", target: header }]);
  assert.equal(h.timers.size, pendingBefore + 1);
});

test("an invalidated extension context stops every observer and timer", async () => {
  const h = createHarness();
  h.doc.body.appendChild(new FakeElement(h.doc, "div", "chat-shell"));
  await loadContentScript(h);
  h.advance(300);
  assert.ok(h.observers.length >= 3);

  h.runtime.id = undefined;
  h.advance(2000); // safety poll notices the invalidation

  for (const observer of h.observers) {
    assert.equal(observer.targets.length, 0, "observer still attached");
  }
  assert.equal(h.timers.size, 0, "timers left running after teardown");

  // Page events after teardown are ignored instead of throwing.
  h.context.dispatchEvent(new h.context.CustomEvent("chatterino-companion-gql", { detail: {} }));
  h.context.dispatchEvent(new h.context.CustomEvent("chatterino-companion-reward-pending", { detail: {} }));
  h.context.dispatchEvent(new h.context.CustomEvent("chatterino-companion-minute-watched-result", { detail: {} }));
  assert.equal(h.timers.size, 0);
});

test("teardown removes the toolbar replicas instead of leaving stale copies", async () => {
  const h = createHarness();
  h.doc.body.appendChild(new FakeElement(h.doc, "div", "chat-shell"));
  await loadContentScript(h);
  h.advance(300);
  const portal = h.doc.getElementById("chatterino-toolbar-portal");
  assert.ok(portal);
  const slot = h.doc.getElementById("chatterino-toolbar-slot");
  for (const id of ["chatterino-points-replica", "chatterino-badge-replica", "chatterino-poll-replica"]) {
    const replica = new FakeElement(h.doc, "div");
    replica.id = id;
    slot.appendChild(replica);
  }
  h.doc.documentElement.classList.add("chatterino-points-replica-active");
  h.doc.documentElement.classList.add("chatterino-badge-replica-active");

  h.runtime.id = undefined;
  h.advance(2000);

  assert.equal(h.doc.getElementById("chatterino-toolbar-portal"), null);
  for (const id of ["chatterino-points-replica", "chatterino-badge-replica", "chatterino-poll-replica"]) {
    assert.equal(h.doc.getElementById(id), null, `${id} left in the page`);
  }
  assert.equal(h.doc.documentElement.classList.contains("chatterino-points-replica-active"), false);
  assert.equal(h.doc.documentElement.classList.contains("chatterino-badge-replica-active"), false);
  assert.equal(h.windowListeners.scroll.length, 0);
  assert.equal(h.windowListeners.resize.length, 0);
  assert.equal(h.timers.size, 0);
});

test("the toolbar portal installs its scroll and resize listeners once and coalesces repositioning", async () => {
  const h = createHarness();
  await loadContentScript(h);
  h.advance(300);
  h.doc.getElementById("chatterino-toolbar-portal").remove();
  h.advance(2500); // next sync rebuilds the portal
  assert.ok(h.doc.getElementById("chatterino-toolbar-portal"));
  assert.equal(h.windowListeners.scroll.length, 1);
  assert.equal(h.windowListeners.resize.length, 1);

  const pendingBefore = h.timers.size;
  for (let i = 0; i < 20; i++) {
    h.windowListeners.scroll[0]({ type: "scroll" });
  }
  assert.equal(h.timers.size, pendingBefore + 1, "scroll events were not coalesced");
});

async function loadAntiWipe() {
  const classes = new FakeClassList();
  const attributes = {};
  const dispatched = [];
  const storageWrites = [];
  let observerCallback = null;
  const shellChild = { innerText: "Chatterino should show here: ..." };
  const context = {
    console: { log() {}, error() {} },
    document: {
      documentElement: {
        classList: classes,
        setAttribute: (k, v) => (attributes[k] = v),
        hasAttribute: (k) => k in attributes,
      },
      head: { appendChild() {} },
      createElement: () => ({}),
      querySelector: (sel) => (sel === ".chat-shell" ? { children: [shellChild] } : null),
    },
    MutationObserver: class {
      constructor(cb) {
        observerCallback = cb;
      }
      observe() {}
    },
    CustomEvent: class {
      constructor(type) {
        this.type = type;
      }
    },
    Element: class {},
    chrome: {
      runtime: { id: "x", getURL: (f) => f },
      storage: { local: { set: (v) => storageWrites.push(v) } },
    },
  };
  context.window = {
    addEventListener: (type, fn) => {
      context.window[`on${type}`] = fn;
    },
    dispatchEvent: (event) => {
      dispatched.push(event.type);
      context.window[`on${event.type}`]?.(event);
    },
  };
  vm.createContext(context);
  vm.runInContext(
    await readFile(new URL("../anti-wipe.js", import.meta.url), "utf8"),
    context
  );
  return { dispatched, storageWrites, runObserver: () => observerCallback([]) };
}

test("anti-wipe activates companion mode once instead of on every mutation batch", async () => {
  const h = await loadAntiWipe();
  // Initial detection at load.
  assert.equal(h.dispatched.filter((t) => t === "chatterino-companion-active").length, 1);
  for (let i = 0; i < 50; i++) {
    h.runObserver();
  }
  assert.equal(h.dispatched.filter((t) => t === "chatterino-companion-active").length, 1);
  assert.equal(h.storageWrites.length, 0, "chrome.storage written on every batch");
});

async function loadOverlay() {
  const shells = [{ children: [{}], getBoundingClientRect: () => ({ x: 0, y: 0, width: 300, height: 700 }) }];
  const observed = [];
  const intervals = new Map();
  let nextId = 1;
  let throwOnSend = false;
  const runtime = {
    id: "x",
    sendMessage: (message, callback) => {
      if (throwOnSend) throw new Error("Extension context invalidated.");
      if (message.type === "get-setting") return callback(true);
      if (message.type === "get-os") return callback("win");
    },
    onMessage: { addListener() {} },
  };
  const windowEvents = {};
  const location = { href: "https://www.twitch.tv/first", pathname: "/first" };
  const disconnected = [];
  const context = vm.createContext({
    console: { log() {}, warn() {}, error() {} },
    location,
    document: {
      fullscreenElement: null,
      documentElement: { classList: { add() {}, remove() {}, contains: () => false }, setAttribute() {}, removeAttribute() {} },
      addEventListener() {},
      getElementsByClassName: (name) => (name === "chat-shell" ? shells : []),
    },
    window: { location, devicePixelRatio: 1, addEventListener: (e, fn) => (windowEvents[e] = fn), dispatchEvent() {} },
    chrome: { runtime, storage: { onChanged: { addListener() {} } } },
    ResizeObserver: class {
      constructor(cb) {
        this.cb = cb;
      }
      observe(el) {
        observed.push(el);
      }
      unobserve(el) {
        observed.splice(observed.indexOf(el), 1);
      }
      disconnect() {
        disconnected.push("resize");
        observed.length = 0;
      }
    },
    MutationObserver: class {
      observe() {}
      disconnect() {
        disconnected.push("mutation");
      }
    },
    CustomEvent: class {},
    setTimeout: () => 0,
    clearTimeout() {},
    setInterval: (fn) => {
      const id = nextId++;
      intervals.set(id, fn);
      return id;
    },
    clearInterval: (id) => intervals.delete(id),
  });
  context.ChatterinoLifecycle = { leaseDelay: () => null };
  vm.runInContext(await readFile(new URL("../overlay.js", import.meta.url), "utf8"), context);
  return {
    shells, observed, intervals, location, windowEvents, disconnected,
    invalidate() {
      runtime.id = undefined;
      throwOnSend = true;
    },
  };
}

test("overlay keeps measuring the chat shell that replaced the original one", async () => {
  const h = await loadOverlay();
  assert.equal(h.observed.length, 1);
  const first = h.shells[0];
  assert.equal(h.observed[0], first);

  h.shells[0] = { children: [{}], getBoundingClientRect: () => ({ x: 0, y: 0, width: 300, height: 700 }) };
  h.location.pathname = "/second";
  h.location.href = "https://www.twitch.tv/second";
  for (const fn of [...h.intervals.values()]) fn();
  assert.equal(h.observed.length, 1);
  assert.equal(h.observed[0], h.shells[0]);
});

test("overlay stops its path poll and observers once the extension context is gone", async () => {
  const h = await loadOverlay();
  h.invalidate();
  assert.doesNotThrow(() => h.windowEvents.resize());
  assert.equal(h.intervals.size, 0);
  assert.ok(h.disconnected.includes("mutation"));
  assert.ok(h.disconnected.includes("resize"));
});
