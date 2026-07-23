import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

class FakeXmlHttpRequest {
  constructor() {
    this.listeners = {};
    this.responseText = '';
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
  const source = await readFile(new URL('../twitch-api.js', import.meta.url), 'utf8');
  const attributes = new Map();
  const events = [];
  const windowListeners = new Map();
  const window = {
    location: { pathname: '/example' },
    fetch: async () => ({
      ok: false,
      clone() {
        return { text: async () => '' };
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
    XMLHttpRequest: FakeXmlHttpRequest,
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

  return { window, attributes, events };
}

test('a generic CommunityPoll event with outcomes is not misclassified as a prediction', async () => {
  const harness = await createTwitchApiHarness();
  const xhr = new FakeXmlHttpRequest();
  xhr.open('POST', 'https://gql.twitch.tv/gql');
  xhr.responseText = JSON.stringify({
    data: {
      communityPoll: {
        __typename: 'CommunityPoll',
        event: {
          __typename: 'CommunityPollEvent',
          title: 'Which map should be next?',
          outcomes: [{ title: 'Map A' }, { title: 'Map B' }],
          status: 'ACTIVE',
        },
      },
    },
  });
  xhr.send();

  const state = harness.window.__chatterinoCompanionGql.getState();
  assert.equal(state.prediction, null);
  assert.equal(state.poll.title, 'Which map should be next?');
  assert.deepEqual(Array.from(state.poll.options), ['Map A', 'Map B']);
  assert.match(harness.attributes.get('data-cc-gql-poll'), /Which map should be next/);
});
