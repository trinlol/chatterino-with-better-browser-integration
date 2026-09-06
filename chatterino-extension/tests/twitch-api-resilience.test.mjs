import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

class FakeXmlHttpRequest {
  constructor() {
    this.listeners = {};
    this.responseText = "";
    this.addCalls = 0;
  }

  addEventListener(name, listener) {
    this.addCalls += 1;
    if (this.throwOnAdd) throw new Error("listener rejected");
    this.listeners[name] = listener;
  }

  open(_method, url) {
    this.url = url;
  }

  send() {
    this.listeners.load?.call(this);
    return "original-xhr-result";
  }
}

async function createHarness({
  pathname = "/example",
  visibilityState = "visible",
  fetchImpl = async () => ({
    ok: true,
    status: 200,
    json: async () => ({ data: {} }),
  }),
} = {}) {
  let now = 1_000_000;
  const timers = [];
  const attributes = new Map();
  const events = [];
  const windowListeners = new Map();
  const documentListeners = new Map();
  const FakeDate = class extends Date {
    static now() {
      return now;
    }
  };
  const window = {
    location: { pathname },
    fetch: fetchImpl,
    addEventListener(name, listener) {
      windowListeners.set(name, listener);
    },
    dispatchEvent(event) {
      events.push(event);
      windowListeners.get(event.type)?.(event);
    },
  };
  const document = {
    visibilityState,
    documentElement: {
      setAttribute(name, value) {
        attributes.set(name, value);
      },
    },
    addEventListener(name, listener) {
      documentListeners.set(name, listener);
    },
  };
  class HarnessXmlHttpRequest extends FakeXmlHttpRequest {}
  const context = vm.createContext({
    window,
    document,
    XMLHttpRequest: HarnessXmlHttpRequest,
    CustomEvent: class CustomEvent {
      constructor(type, init = {}) {
        this.type = type;
        this.detail = init.detail;
      }
    },
    Headers,
    AbortController,
    Date: FakeDate,
    JSON,
    Object,
    Array,
    String,
    Boolean,
    Number,
    Promise,
    structuredClone,
    setTimeout(callback, delay) {
      const timer = { callback, delay, cleared: false };
      timers.push(timer);
      return timer;
    },
    clearTimeout(timer) {
      if (timer) timer.cleared = true;
    },
  });
  const source = await readFile(
    new URL("../twitch-api.js", import.meta.url),
    "utf8"
  );
  vm.runInContext(source, context);
  return {
    window,
    document,
    context,
    timers,
    attributes,
    events,
    XmlHttpRequest: HarnessXmlHttpRequest,
    setNow(value) {
      now = value;
    },
    runAgain() {
      vm.runInContext(source, context);
    },
  };
}

test("injection is idempotent and observer failures never alter Twitch fetch or XHR results", async () => {
  let cloneCalls = 0;
  const harness = await createHarness({
    fetchImpl: async () => ({
      ok: true,
      status: 200,
      clone() {
        cloneCalls += 1;
        throw new Error("non-Twitch clone must not run");
      },
    }),
  });
  const hookedFetch = harness.window.fetch;
  harness.runAgain();
  assert.equal(harness.window.fetch, hookedFetch);
  const response = await harness.window.fetch("https://example.test/api");
  assert.equal(response.ok, true);
  assert.equal(cloneCalls, 0);

  const xhr = new harness.XmlHttpRequest();
  xhr.throwOnAdd = true;
  xhr.open("POST", "https://gql.twitch.tv/gql");
  assert.equal(xhr.send(), "original-xhr-result");
});

test("hidden or channel-less pages suspend private GraphQL polling", async () => {
  let fetches = 0;
  const hidden = await createHarness({
    visibilityState: "hidden",
    fetchImpl: async () => {
      fetches += 1;
      return { ok: true, status: 200, json: async () => ({ data: {} }) };
    },
  });
  assert.equal(hidden.timers.length, 0);
  assert.equal(
    await hidden.window.__chatterinoCompanionGql.refreshContext(),
    false
  );

  const noChannel = await createHarness({ pathname: "/directory" });
  assert.equal(noChannel.timers.length, 0);
  assert.equal(
    await noChannel.window.__chatterinoCompanionGql.refreshContext(),
    false
  );
  assert.equal(fetches, 0);
});

test("timeouts, malformed payloads, rate limits, and recovery are bounded and observable", async () => {
  let aborted = false;
  const timeoutHarness = await createHarness({
    fetchImpl: (_url, init) =>
      new Promise((_resolve, reject) => {
        init.signal.addEventListener("abort", () => {
          aborted = true;
          reject(new Error("aborted"));
        });
      }),
  });
  const refresh =
    timeoutHarness.window.__chatterinoCompanionGql.refreshContext();
  const timeout = timeoutHarness.timers.find((timer) => timer.delay === 10000);
  timeout.callback();
  assert.equal(await refresh, false);
  assert.equal(aborted, true);
  assert.equal(
    timeoutHarness.window.__chatterinoCompanionGql.getAdapterStatus().failure,
    "network"
  );

  const responses = [429, 429, 429, 200];
  let calls = 0;
  const harness = await createHarness({
    fetchImpl: async () => {
      const status = responses[calls++] ?? 200;
      return {
        ok: status === 200,
        status,
        json: async () => ({
          data: { community: { channel: { login: "example", id: "1" } } },
        }),
      };
    },
  });
  for (let attempt = 0; attempt < 3; attempt += 1) {
    assert.equal(
      await harness.window.__chatterinoCompanionGql.refreshContext(),
      false
    );
  }
  const blocked = harness.window.__chatterinoCompanionGql.getAdapterStatus();
  assert.equal(blocked.failure, "rate-limited");
  assert.ok(blocked.cooldownUntil > 0);
  assert.equal(calls, 3);
  assert.equal(
    await harness.window.__chatterinoCompanionGql.refreshContext(),
    false
  );
  assert.equal(calls, 3);

  harness.setNow(blocked.cooldownUntil + 1);
  assert.equal(
    await harness.window.__chatterinoCompanionGql.refreshContext(),
    true
  );
  const recovered = harness.window.__chatterinoCompanionGql.getAdapterStatus();
  assert.equal(recovered.supported, true);
  assert.equal(recovered.failure, "");

  const xhr = new harness.XmlHttpRequest();
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.responseText = "{malformed";
  assert.doesNotThrow(() => xhr.send());
  assert.equal(
    harness.window.__chatterinoCompanionGql.getAdapterStatus().failure,
    "schema"
  );
});

test("missing, renamed, and null GraphQL shapes degrade without publishing fake activity", async () => {
  const harness = await createHarness();
  for (const responseText of [
    JSON.stringify({ data: {} }),
    JSON.stringify({ data: { renamedActivity: { title: "not a poll" } } }),
    JSON.stringify({ data: null }),
  ]) {
    const xhr = new harness.XmlHttpRequest();
    xhr.open("POST", "https://gql.twitch.tv/gql");
    xhr.responseText = responseText;
    assert.doesNotThrow(() => xhr.send());
  }
  const state = harness.window.__chatterinoCompanionGql.getState();
  assert.equal(state.poll, null);
  assert.equal(state.prediction, null);
  assert.equal(state.adapter.source, "private-graphql");
});
