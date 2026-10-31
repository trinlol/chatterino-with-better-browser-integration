import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

class FakeXmlHttpRequest {
  constructor() {
    this.listeners = {};
    this.responseText = "";
    this.responseType = "";
    this.addCalls = 0;
  }

  addEventListener(name, listener) {
    this.addCalls += 1;
    (this.listeners[name] ||= []).push(listener);
  }

  open(_method, url) {
    this.url = url;
  }

  send() {
    for (const listener of this.listeners.load || []) listener.call(this);
  }
}

function okJson(body, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
    clone() {
      return { text: async () => JSON.stringify(body) };
    },
  };
}

async function createHarness({ pathname = "/example", fetchImpl } = {}) {
  let now = 1_000_000;
  const requests = [];
  const attributes = new Map();
  const events = [];
  const windowListeners = new Map();
  const FakeDate = class extends Date {
    static now() {
      return now;
    }
  };
  const window = {
    location: { pathname },
    fetch: async (url, init = {}) => {
      requests.push({ url, init });
      return fetchImpl ? fetchImpl(url, init) : okJson({ data: {} });
    },
    addEventListener(name, listener) {
      windowListeners.set(name, listener);
    },
    dispatchEvent(event) {
      events.push(event);
      windowListeners.get(event.type)?.(event);
    },
  };
  const document = {
    visibilityState: "visible",
    documentElement: {
      setAttribute(name, value) {
        attributes.set(name, value);
      },
    },
    addEventListener() {},
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
    structuredClone,
    btoa,
    setTimeout: () => 1,
    clearTimeout() {},
  });
  const source = await readFile(
    new URL("../twitch-api.js", import.meta.url),
    "utf8"
  );
  vm.runInContext(source, context);
  const api = window.__chatterinoCompanionGql;
  return {
    window,
    api,
    attributes,
    events,
    requests,
    setNow(value) {
      now = value;
    },
    advance(ms) {
      now += ms;
    },
    xhr(body) {
      const xhr = new HarnessXmlHttpRequest();
      xhr.open("POST", "https://gql.twitch.tv/gql");
      xhr.responseText = typeof body === "string" ? body : JSON.stringify(body);
      xhr.send();
      return xhr;
    },
    lastSnapshot() {
      return events.filter((e) => e.type === "chatterino-companion-gql").at(-1)
        ?.detail;
    },
  };
}

async function loadPageData() {
  const source = await readFile(
    new URL("../page-data.js", import.meta.url),
    "utf8"
  );
  const window = {};
  vm.runInNewContext(source, { window });
  return window.ChatterinoPageData;
}

function headerValue(init, name) {
  const headers = init?.headers || {};
  const key = Object.keys(headers).find(
    (k) => k.toLowerCase() === name.toLowerCase()
  );
  return key ? headers[key] : undefined;
}

test("non-GraphQL requests never contribute headers or hashes to replayed requests", async () => {
  let cloneCalls = 0;
  const harness = await createHarness({
    fetchImpl: (url) => {
      const response = okJson({ data: {} });
      if (!String(url).startsWith("https://gql.twitch.tv/")) {
        response.clone = () => {
          cloneCalls += 1;
          return { text: async () => "{}" };
        };
      }
      return response;
    },
  });
  const evilBody = JSON.stringify({
    operationName: "ChannelPointsContext",
    extensions: { persistedQuery: { sha256Hash: "evilhash" } },
  });
  await harness.window.fetch("https://7tv.io/v3/gql", {
    method: "POST",
    headers: { Authorization: "Bearer third-party", "Client-Id": "evil-id" },
    body: evilBody,
  });
  await harness.window.fetch("https://gql.twitch.tv.evil.test/gql", {
    headers: new Headers({ Authorization: "Bearer lookalike" }),
    body: evilBody,
  });
  await harness.window.fetch({
    url: "https://api.example.test/",
    headers: new Headers({ Authorization: "Bearer request-object" }),
  });
  assert.equal(cloneCalls, 0, "non-GQL response bodies are not read");

  harness.requests.length = 0;
  await harness.api.refreshContext();
  const replay = harness.requests[0];
  assert.match(String(replay.url), /^https:\/\/gql\.twitch\.tv\/gql$/);
  assert.equal(headerValue(replay.init, "Authorization"), undefined);
  assert.equal(headerValue(replay.init, "Client-Id"), "kimne78zx3cx6dzkoethbq4z55auq1");
  assert.doesNotMatch(replay.init.body, /evilhash/);

  // Twitch's own GQL request is captured, replayed only to gql.twitch.tv, and
  // never published.
  await harness.window.fetch("https://gql.twitch.tv/gql", {
    headers: [["Authorization", "OAuth twitch-token"], ["Client-Id", "twitch-id"]],
    body: "[]",
  });
  harness.requests.length = 0;
  await harness.api.refreshContext();
  assert.equal(headerValue(harness.requests[0].init, "Authorization"), "OAuth twitch-token");
  assert.equal(headerValue(harness.requests[0].init, "Client-Id"), "twitch-id");
  for (const value of harness.attributes.values()) {
    assert.doesNotMatch(String(value), /twitch-token/);
  }
  for (const event of harness.events) {
    assert.doesNotMatch(JSON.stringify(event.detail ?? null), /twitch-token/);
  }
});

test("own requests are not parsed twice by the fetch observer", async () => {
  let clones = 0;
  const harness = await createHarness({
    fetchImpl: () => {
      const response = okJson({ data: {} });
      response.clone = () => {
        clones += 1;
        return { text: async () => "{}" };
      };
      return response;
    },
  });
  await harness.api.refreshContext();
  assert.equal(clones, 0);
});

test("prediction lifecycle: started, locked, ended, cancelled clears, channel switch resets", async () => {
  const harness = await createHarness();
  const send = (status) =>
    harness.xhr({
      data: {
        communityPredictionEvent: {
          title: "Will we win?",
          outcomes: [{ title: "Yes" }, { title: "No" }],
          status,
          predictionWindowSeconds: 60,
          createdAt: 2_000_000_000_000,
        },
      },
    });
  send("ACTIVE");
  assert.equal(harness.api.getState().prediction.status, "started");
  assert.equal(harness.api.getState().prediction.closesAt, 2_000_000_060_000);
  send("LOCKED");
  assert.equal(harness.api.getState().prediction.status, "locked");
  send("RESOLVED");
  assert.equal(harness.api.getState().prediction.status, "ended");
  send("ACTIVE");
  send("CANCELED");
  assert.equal(harness.api.getState().prediction, null);
  assert.equal(harness.attributes.get("data-cc-gql-prediction"), "");
  send("ACTIVE");
  send("CANCEL_PENDING");
  assert.equal(harness.api.getState().prediction, null);

  send("ACTIVE");
  harness.window.dispatchEvent({
    type: "chatterino-companion-channel-change",
    detail: { channel: 42 },
  });
  const state = harness.api.getState();
  assert.equal(state.prediction, null);
  assert.equal(state.channelPoints.channelLogin, "example");
});

test("poll lifecycle and remaining-duration deadlines", async () => {
  const harness = await createHarness();
  const send = (event) =>
    harness.xhr({
      data: {
        communityPollEvent: {
          title: "Next map?",
          choices: [{ title: "A" }, { title: "B" }],
          ...event,
        },
      },
    });
  harness.setNow(5_000_000);
  send({ status: "ACTIVE", remainingDurationSeconds: 30, startedAt: 1 });
  assert.equal(harness.api.getState().poll.closesAt, 5_030_000);
  send({ status: "TERMINATED" });
  assert.equal(harness.api.getState().poll.status, "ended");
  send({ status: "ACTIVE" });
  send({ status: "ARCHIVED" });
  assert.equal(harness.api.getState().poll, null);
  send({ status: "ACTIVE" });
  send({ status: "MODERATED" });
  assert.equal(harness.api.getState().poll, null);
});

test("malformed shapes do not abort extraction of the rest of the payload", async () => {
  const harness = await createHarness();
  harness.xhr({
    data: {
      communityPredictionEvent: { title: "Odd", outcomes: { edges: [] }, status: 7 },
      communityPollEvent: { title: "Poll", choices: [null, 5, { title: "A" }] },
      user: { channel: { login: 12345, id: 9 } },
      community: {
        channel: {
          login: "example",
          id: 77,
          self: { communityPoints: { balance: 2500, availableClaim: { id: "claim-1" } } },
        },
      },
    },
  });
  const state = harness.api.getState();
  assert.equal(state.prediction.title, "Odd");
  assert.deepEqual(Array.from(state.poll.options), ["A"]);
  assert.equal(state.channelPoints.balance, "2.5K");
  assert.equal(state.channelPoints.channelId, "77");
  assert.equal(state.channelPoints.claimId, "claim-1");
  assert.equal(state.adapter.supported, true);
});

test("huge and deeply nested payloads are bounded and never throw", async () => {
  const harness = await createHarness();
  const deep = '{"a":'.repeat(5000) + "1" + "}".repeat(5000);
  assert.doesNotThrow(() => harness.xhr(`{"data":${deep}}`));
  assert.doesNotThrow(() => harness.xhr("x".repeat(9 * 1024 * 1024)));
  assert.equal(harness.api.getAdapterStatus().cooldownUntil, 0);
});

test("a batch with one failed operation still applies the others; observed failures do not open the circuit", async () => {
  const harness = await createHarness();
  harness.xhr([
    { errors: [{ message: "PersistedQueryNotFound" }] },
    {
      data: {
        community: {
          channel: { login: "example", id: "1", self: { communityPoints: { balance: 1500 } } },
        },
      },
    },
  ]);
  assert.equal(harness.api.getState().channelPoints.balance, "1.5K");
  assert.equal(harness.api.getAdapterStatus().supported, true);

  for (let i = 0; i < 5; i += 1) {
    harness.xhr({ errors: [{ message: "integrity" }] });
  }
  const status = harness.api.getAdapterStatus();
  assert.equal(status.failure, "schema");
  assert.equal(status.cooldownUntil, 0);
  harness.requests.length = 0;
  await harness.api.refreshContext();
  assert.equal(harness.requests.length, 1);
});

test("reused XHRs attach the observer once and json responses are read", async () => {
  const harness = await createHarness();
  const XHR = harness.xhr("{}").constructor;
  const xhr = new XHR();
  xhr.responseType = "json";
  xhr.response = {
    data: { community: { channel: { login: "example", self: { communityPoints: { balance: 10 } } } } },
  };
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.send();
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.send();
  assert.equal(xhr.addCalls, 1);
  assert.equal(harness.api.getState().channelPoints.balance, "10");
});

test("published balances and ids pass page-data validation", async () => {
  const pageData = await loadPageData();
  const harness = await createHarness();
  for (const [raw, expected] of [
    [999_999, "1M"],
    [999_950_000, "1B"],
    [1_500, "1.5K"],
    [42, "42"],
    ["7300", "7.3K"],
    [-5, ""],
  ]) {
    harness.xhr({
      data: {
        community: {
          channel: {
            login: "example",
            id: 123,
            self: { communityPoints: { balance: raw, availableClaim: { id: "abc-123" } } },
          },
        },
      },
    });
    const attr = harness.attributes.get("data-cc-gql-balance");
    if (expected) {
      assert.equal(attr, expected, `balance ${raw}`);
      assert.equal(pageData.sanitizeBalance(attr), attr);
    }
    const snapshot = pageData.sanitizeSnapshot(harness.lastSnapshot());
    assert.equal(snapshot.channelPoints.channelId, "123");
    assert.equal(snapshot.channelPoints.claimId, "abc-123");
  }
  harness.xhr({
    data: {
      communityPredictionEvent: {
        title: "Will we win?",
        outcomes: [{ title: "Yes" }, { title: "No" }],
        status: "ACTIVE",
      },
    },
  });
  const activity = pageData.parseActivityAttribute(
    harness.attributes.get("data-cc-gql-prediction")
  );
  assert.equal(activity.title, "Will we win?");
  assert.deepEqual(Array.from(activity.options), ["Yes", "No"]);
});

test("claim requests are throttled even when the claim id must be fetched", async () => {
  const harness = await createHarness();
  harness.xhr({
    data: {
      community: {
        channel: { login: "example", self: { communityPoints: { claimAvailable: true } } },
      },
    },
  });
  harness.requests.length = 0;
  for (let i = 0; i < 5; i += 1) {
    await harness.api.claimChannelPoints();
  }
  assert.equal(harness.requests.length, 1);
});

test("minute-watched sends are throttled per stream and back off after 429", async () => {
  let status = 200;
  const harness = await createHarness({
    fetchImpl: () => okJson({ data: { sendSpadeEvents: { statusCode: 204 } } }, status),
  });
  const detail = { channelId: "1", streamId: "100", channel: "example" };
  assert.equal((await harness.api.sendMinuteWatched(detail)).ok, true);
  const second = await harness.api.sendMinuteWatched(detail);
  assert.equal(second.ok, false);
  assert.equal(second.error, "rate-limited");
  assert.equal(harness.requests.length, 1);

  status = 429;
  harness.advance(60_000);
  assert.equal((await harness.api.sendMinuteWatched(detail)).ok, false);
  const blocked = await harness.api.sendMinuteWatched({ ...detail, streamId: "200" });
  assert.equal(blocked.error, "backoff");
  assert.equal(harness.requests.length, 2);

  status = 200;
  harness.advance(61_000);
  assert.equal((await harness.api.sendMinuteWatched({ ...detail, streamId: "200" })).ok, true);
  assert.match(String(harness.requests.at(-1).url), /^https:\/\/gql\.twitch\.tv\//);
});
