import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

// XHR whose response is delivered later by the test (respond()), so a
// request can be issued on one channel and answered on another.
class DeferredXmlHttpRequest {
  constructor() {
    this.listeners = {};
    this.responseText = "";
    this.responseType = "";
  }

  addEventListener(name, listener) {
    (this.listeners[name] ||= []).push(listener);
  }

  open(_method, url) {
    this.url = url;
  }

  send() {}

  respond(body) {
    this.responseText = typeof body === "string" ? body : JSON.stringify(body);
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
  const attributeWrites = [];
  const events = [];
  const timers = [];
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
        attributeWrites.push(name);
        attributes.set(name, value);
      },
    },
    addEventListener() {},
  };
  // A class per harness: twitch-api.js patches the prototype it is given.
  class HarnessXmlHttpRequest extends DeferredXmlHttpRequest {}
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
    setTimeout(callback, delay) {
      const timer = { callback, at: now + (Number(delay) || 0), cleared: false };
      timers.push(timer);
      return timer;
    },
    clearTimeout(timer) {
      if (timer && typeof timer === "object") timer.cleared = true;
    },
  });
  const source = await readFile(
    new URL("../twitch-api.js", import.meta.url),
    "utf8"
  );
  vm.runInContext(source, context);
  const api = window.__chatterinoCompanionGql;
  const gqlEvents = () => events.filter((e) => e.type === "chatterino-companion-gql");
  return {
    window,
    api,
    attributes,
    attributeWrites,
    events,
    gqlEvents,
    requests,
    advance(ms) {
      now += ms;
    },
    // Runs due timers other than the 1 s / 45 s context polls.
    runDueTimers({ skipPolling = true } = {}) {
      for (const timer of timers.splice(0)) {
        if (timer.cleared) continue;
        if (timer.at > now) {
          timers.push(timer);
          continue;
        }
        if (skipPolling && timer.callback.toString().includes("refreshChannelPointsContext")) {
          continue;
        }
        timer.callback();
      }
    },
    navigate(login) {
      window.location.pathname = `/${login}`;
    },
    channelChange(login) {
      window.dispatchEvent({
        type: "chatterino-companion-channel-change",
        detail: { channel: login },
      });
    },
    request(requestBody) {
      const xhr = new HarnessXmlHttpRequest();
      xhr.open("POST", "https://gql.twitch.tv/gql");
      xhr.send(requestBody === undefined ? undefined : JSON.stringify(requestBody));
      return xhr;
    },
    xhr(body, requestBody) {
      const xhr = this.request(requestBody);
      xhr.respond(body);
      return xhr;
    },
  };
}

const pollResponse = (title = "Next map?", status = "ACTIVE") => ({
  data: {
    communityPollEvent: {
      title,
      choices: [{ title: "A" }, { title: "B" }],
      status,
    },
  },
});

const predictionResponse = (status = "ACTIVE") => ({
  data: {
    communityPredictionEvent: {
      title: "Will we win?",
      outcomes: [{ title: "Yes" }, { title: "No" }],
      status,
    },
  },
});

test("a late response to a request issued before a channel switch is ignored", async () => {
  const harness = await createHarness({ pathname: "/oldchan" });
  const pending = harness.request(); // no channel in the variables
  harness.navigate("newchan");
  harness.channelChange("newchan");
  pending.respond(pollResponse("Old channel poll"));
  assert.equal(harness.api.getState().poll, null);
  assert.equal(harness.attributes.get("data-cc-gql-poll"), "");

  // Requests issued after the reset are applied as before.
  harness.xhr(pollResponse("New channel poll"));
  assert.equal(harness.api.getState().poll.title, "New channel poll");
});

test("request variables decide which channel a response belongs to", async () => {
  const harness = await createHarness({ pathname: "/oldchan" });

  // Issued for the new channel right after navigation, before content.js
  // announced the channel change: still applied.
  harness.navigate("newchan");
  const early = harness.request({ operationName: "Polls", variables: { channelLogin: "NewChan" } });
  harness.channelChange("newchan");
  early.respond(pollResponse("Fresh poll"));
  assert.equal(harness.api.getState().poll.title, "Fresh poll");

  // Issued after the reset but naming the previous channel: ignored.
  harness.xhr(predictionResponse(), { variables: { channelLogin: "oldchan" } });
  assert.equal(harness.api.getState().prediction, null);

  // Batches are matched entry by entry.
  harness.xhr(
    [predictionResponse(), pollResponse("Batched current poll")],
    [
      { variables: { channelLogin: "oldchan" } },
      { variables: { channelLogin: "newchan" } },
    ]
  );
  assert.equal(harness.api.getState().prediction, null);
  assert.equal(harness.api.getState().poll.title, "Batched current poll");
});

test("login-keyed variables and the navigation gap keep the new channel's responses", async () => {
  const harness = await createHarness({ pathname: "/oldchan" });

  // Twitch's route queries use `login` / `input.channelLogin`.
  harness.xhr(pollResponse("Other channel"), { variables: { login: "elsewhere" } });
  assert.equal(harness.api.getState().poll, null);
  harness.xhr(predictionResponse(), { variables: { input: { channelLogin: "elsewhere" } } });
  assert.equal(harness.api.getState().prediction, null);

  // The URL changes, Twitch fires the new route's requests at once, and
  // content.js announces the channel change only afterwards.
  harness.navigate("newchan");
  const byLogin = harness.request({ variables: { login: "newchan" } });
  const byId = harness.request({ variables: { channelID: "999" } });
  const bare = harness.request();
  harness.channelChange("newchan");
  byLogin.respond(pollResponse("Viewable poll"));
  assert.equal(harness.api.getState().poll.title, "Viewable poll");
  byId.respond(predictionResponse());
  assert.equal(harness.api.getState().prediction.title, "Will we win?");
  bare.respond({
    data: { community: { channel: { login: "newchan", id: "999", self: { communityPoints: { balance: 5 } } } } },
  });
  assert.equal(harness.api.getState().channelPoints.balance, "5");

  // A request issued while the URL still showed the old channel is dropped.
  const old = harness.request();
  harness.navigate("thirdchan");
  harness.channelChange("thirdchan");
  old.respond(pollResponse("Late old poll"));
  assert.equal(harness.api.getState().poll, null);
});

test("initial load: requests made before the first channel announcement are kept", async () => {
  const harness = await createHarness({ pathname: "/example" });
  const pending = harness.request({ variables: { channelID: "1" } });
  // content.js starts with currentChannel "" and always announces once.
  harness.channelChange("example");
  pending.respond(pollResponse("Initial poll"));
  assert.equal(harness.api.getState().poll.title, "Initial poll");
});

test("published freshness never trails by more than the 15 s heartbeat", async () => {
  const harness = await createHarness();
  harness.xhr(pollResponse());
  const before = harness.gqlEvents().length;
  harness.advance(14_000);
  harness.xhr(pollResponse());
  assert.equal(harness.gqlEvents().length, before);
  harness.advance(1_000);
  harness.xhr(pollResponse());
  assert.equal(harness.gqlEvents().length, before + 1);
  assert.equal(harness.gqlEvents().at(-1).detail.adapter.freshUntil, 1_015_000 + 90_000);
});

test("observed fetch responses carry their request's channel too", async () => {
  const harness = await createHarness({
    pathname: "/newchan",
    fetchImpl: () => okJson(pollResponse("Other channel poll")),
  });
  await harness.window.fetch("https://gql.twitch.tv/gql", {
    method: "POST",
    body: JSON.stringify({ variables: { channelLogin: "otherchan" } }),
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(harness.api.getState().poll, null);

  await harness.window.fetch("https://gql.twitch.tv/gql", {
    method: "POST",
    body: JSON.stringify({ variables: { channelLogin: "newchan" } }),
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(harness.api.getState().poll.title, "Other channel poll");
});

test("ended polls and predictions expire after 30 s and are not resurrected", async () => {
  const harness = await createHarness();
  harness.xhr(pollResponse("Next map?", "ACTIVE"));
  harness.xhr(pollResponse("Next map?", "COMPLETED"));
  assert.equal(harness.api.getState().poll.status, "ended");

  harness.advance(20_000);
  harness.xhr(pollResponse("Next map?", "COMPLETED")); // repeat keeps the first end time
  harness.runDueTimers();
  assert.equal(harness.api.getState().poll.status, "ended");

  harness.advance(10_100);
  harness.runDueTimers();
  assert.equal(harness.api.getState().poll, null);
  assert.equal(harness.attributes.get("data-cc-gql-poll"), "");
  assert.equal(harness.gqlEvents().at(-1).detail.poll, null);

  // Twitch answering with the same finished poll again does not bring it back.
  harness.xhr(pollResponse("Next map?", "COMPLETED"));
  assert.equal(harness.api.getState().poll, null);

  // A new poll (or the same one restarted) is shown again.
  harness.xhr(pollResponse("Next map?", "ACTIVE"));
  assert.equal(harness.api.getState().poll.status, "started");

  // Locked predictions stay; ended ones expire on the next update even
  // without the timer.
  harness.xhr(predictionResponse("LOCKED"));
  harness.advance(60_000);
  harness.xhr({ data: {} });
  assert.equal(harness.api.getState().prediction.status, "locked");
  harness.xhr(predictionResponse("RESOLVED"));
  harness.advance(30_000);
  harness.xhr({ data: {} });
  assert.equal(harness.api.getState().prediction, null);
});

test("unchanged responses skip attribute writes and the state event but keep freshness", async () => {
  const harness = await createHarness();
  harness.xhr(pollResponse());
  const eventsBefore = harness.gqlEvents().length;
  harness.attributeWrites.length = 0;

  harness.advance(1_000);
  harness.xhr(pollResponse());
  assert.equal(harness.gqlEvents().length, eventsBefore, "event dispatched for an unchanged state");
  assert.deepEqual(harness.attributeWrites, ["data-cc-gql-updated"]);
  assert.equal(harness.attributes.get("data-cc-gql-updated"), "1001000");

  // A relevant change publishes once, writing only what changed.
  harness.attributeWrites.length = 0;
  harness.xhr(pollResponse("Another poll"));
  assert.equal(harness.gqlEvents().length, eventsBefore + 1);
  assert.equal(harness.gqlEvents().at(-1).detail.poll.title, "Another poll");
  assert.deepEqual(
    harness.attributeWrites.sort(),
    ["data-cc-gql-adapter", "data-cc-gql-poll", "data-cc-gql-updated"]
  );

  // A heartbeat republishes everything so adapter freshness never lapses.
  harness.advance(15_000);
  harness.attributeWrites.length = 0;
  harness.xhr(pollResponse("Another poll"));
  assert.equal(harness.gqlEvents().length, eventsBefore + 2);
  assert.ok(harness.attributeWrites.length >= 9);
  const adapter = harness.gqlEvents().at(-1).detail.adapter;
  assert.ok(adapter.freshUntil > 1_016_000);

  // Adapter health changes are published immediately.
  harness.xhr("{malformed");
  assert.equal(harness.gqlEvents().length, eventsBefore + 3);
  assert.equal(harness.gqlEvents().at(-1).detail.adapter.failure, "schema");
});

test("minute-watched events are limited to the page's channel and capped overall", async () => {
  const harness = await createHarness({
    fetchImpl: () => okJson({ data: { sendSpadeEvents: { statusCode: 204 } } }),
  });
  const mismatch = await harness.api.sendMinuteWatched({
    channelId: "1",
    streamId: "100",
    channel: "someoneelse",
  });
  assert.equal(mismatch.error, "channel-mismatch");
  assert.equal(harness.requests.length, 0);

  for (let i = 0; i < 3; i += 1) {
    const sent = await harness.api.sendMinuteWatched({
      channelId: "1",
      streamId: String(200 + i),
      channel: "example",
    });
    assert.equal(sent.ok, true);
  }
  const capped = await harness.api.sendMinuteWatched({
    channelId: "1",
    streamId: "300",
    channel: "example",
  });
  assert.equal(capped.error, "rate-limited");
  assert.equal(harness.requests.length, 3);

  harness.advance(60_000);
  assert.equal(
    (await harness.api.sendMinuteWatched({ channelId: "1", streamId: "300", channel: "example" })).ok,
    true
  );
});
