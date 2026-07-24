import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

class FakeXmlHttpRequest {
  constructor() {
    this.listeners = {};
    this.responseText = "";
  }

  addEventListener(name, listener) {
    this.listeners[name] = listener;
  }

  open() {}

  send() {
    this.listeners.load?.call(this);
  }
}

async function createTwitchApiHarness() {
  class HarnessXmlHttpRequest extends FakeXmlHttpRequest {}
  const source = await readFile(
    new URL("../twitch-api.js", import.meta.url),
    "utf8"
  );
  const attributes = new Map();
  const events = [];
  const windowListeners = new Map();
  const window = {
    location: { pathname: "/example" },
    fetch: async () => ({
      ok: false,
      clone() {
        return { text: async () => "" };
      },
    }),
    addEventListener(name, listener) {
      windowListeners.set(name, listener);
    },
    dispatchEvent(event) {
      events.push(event);
    },
  };
  const document = {
    documentElement: {
      setAttribute(name, value) {
        attributes.set(name, value);
      },
    },
    addEventListener() {},
  };

  vm.runInNewContext(source, {
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
    Date,
    JSON,
    Object,
    Array,
    String,
    Boolean,
    Promise,
    structuredClone,
    setTimeout: () => 1,
    clearTimeout() {},
  });

  return { window, attributes, events, XMLHttpRequest: HarnessXmlHttpRequest };
}

test("a generic CommunityPoll event with outcomes is not misclassified as a prediction", async () => {
  const harness = await createTwitchApiHarness();
  const xhr = new harness.XMLHttpRequest();
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.responseText = JSON.stringify({
    data: {
      communityPoll: {
        __typename: "CommunityPoll",
        event: {
          __typename: "CommunityPollEvent",
          title: "Which map should be next?",
          outcomes: [{ title: "Map A" }, { title: "Map B" }],
          status: "ACTIVE",
        },
      },
    },
  });
  xhr.send();

  const state = harness.window.__chatterinoCompanionGql.getState();
  assert.equal(state.prediction, null);
  assert.equal(state.poll.title, "Which map should be next?");
  assert.deepEqual(Array.from(state.poll.options), ["Map A", "Map B"]);
  assert.match(
    harness.attributes.get("data-cc-gql-poll"),
    /Which map should be next/
  );
});

test("prediction timing is published as one absolute lock deadline", async () => {
  const harness = await createTwitchApiHarness();
  const xhr = new harness.XMLHttpRequest();
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.responseText = JSON.stringify({
    data: {
      communityPredictionEvent: {
        __typename: "CommunityPredictionEvent",
        title: "Will we win?",
        outcomes: [{ title: "Yes" }, { title: "No" }],
        status: "ACTIVE",
        predictionWindowSeconds: 120,
        createdAt: "2026-07-24T12:00:00.000Z",
      },
    },
  });
  xhr.send();

  const prediction =
    harness.window.__chatterinoCompanionGql.getState().prediction;
  assert.equal(prediction.title, "Will we win?");
  assert.equal(prediction.closesAt, Date.parse("2026-07-24T12:02:00.000Z"));
});

test("an empty generic outcomes container does not create a fake prediction", async () => {
  const harness = await createTwitchApiHarness();
  const xhr = new harness.XMLHttpRequest();
  xhr.open("POST", "https://gql.twitch.tv/gql");
  xhr.responseText = JSON.stringify({
    data: {
      unrelatedCommunityEvent: {
        event: {
          outcomes: [],
        },
      },
    },
  });
  xhr.send();

  assert.equal(
    harness.window.__chatterinoCompanionGql.getState().prediction,
    null
  );
});
