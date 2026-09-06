import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

class FakeElement {
  constructor({
    interactive = false,
    text = "",
    attributes = {},
    connected = true,
  } = {}) {
    this.children = [];
    this.parentElement = null;
    this.interactive = interactive;
    this.clicked = 0;
    this.textContent = text;
    this.attributes = attributes;
    this.isConnected = connected;
  }

  append(...children) {
    for (const child of children) {
      child.parentElement = this;
      this.children.push(child);
    }
  }

  matches() {
    return this.interactive;
  }

  getAttribute(name) {
    return this.attributes[name] ?? null;
  }

  closest() {
    return null;
  }

  click() {
    this.clicked += 1;
  }
}

async function loadVotingUi() {
  const source = await readFile(
    new URL("../voting-ui.js", import.meta.url),
    "utf8"
  );
  const window = {};
  vm.runInNewContext(source, { window });
  return window.ChatterinoVotingUi;
}

test("replica activation is forwarded to the matching native Twitch control", async () => {
  const votingUi = await loadVotingUi();

  const sourceRoot = new FakeElement();
  const sourceRow = new FakeElement();
  const sourceButton = new FakeElement({ interactive: true });
  const sourceLabel = new FakeElement();
  sourceRoot.append(sourceRow);
  sourceRow.append(sourceButton);
  sourceButton.append(sourceLabel);

  const replicaRoot = new FakeElement();
  const replicaRow = new FakeElement();
  const replicaButton = new FakeElement({ interactive: true });
  const replicaLabel = new FakeElement();
  replicaRoot.append(replicaRow);
  replicaRow.append(replicaButton);
  replicaButton.append(replicaLabel);

  let prevented = false;
  let stopped = false;
  const forwarded = votingUi.forwardActivation(
    {
      target: replicaLabel,
      preventDefault() {
        prevented = true;
      },
      stopPropagation() {
        stopped = true;
      },
    },
    replicaRoot,
    sourceRoot
  );

  assert.equal(forwarded, true);
  assert.equal(sourceButton.clicked, 1);
  assert.equal(prevented, true);
  assert.equal(stopped, true);
});

test("activation falls back to matching control order when Twitch wrappers differ", async () => {
  const votingUi = await loadVotingUi();

  const sourceRoot = new FakeElement();
  const sourceWrapper = new FakeElement();
  const sourceButton = new FakeElement({
    interactive: true,
    text: "Vote Yes",
  });
  sourceRoot.append(sourceWrapper);
  sourceWrapper.append(sourceButton);

  const replicaRoot = new FakeElement();
  const replicaButton = new FakeElement({
    interactive: true,
    text: "Vote Yes",
  });
  replicaRoot.append(replicaButton);

  const forwarded = votingUi.forwardActivation(
    { target: replicaButton },
    replicaRoot,
    sourceRoot
  );

  assert.equal(forwarded, true);
  assert.equal(sourceWrapper.clicked, 0);
  assert.equal(sourceButton.clicked, 1);
});

test("prediction fallback activates Twitch's native prediction trigger", async () => {
  const votingUi = await loadVotingUi();
  const nativeButton = new FakeElement({
    interactive: true,
    text: "Prediction",
    attributes: { "aria-label": "Open Prediction" },
  });
  const document = {
    querySelectorAll() {
      return [nativeButton];
    },
  };

  assert.equal(votingUi.activateVotingTrigger(document, "prediction"), true);
  assert.equal(nativeButton.clicked, 1);
});

test("fullscreen detection covers the Fullscreen API and viewport-filling video", async () => {
  const votingUi = await loadVotingUi();

  assert.equal(
    votingUi.isFullscreenActive(
      { fullscreenElement: {} },
      { innerWidth: 1920, innerHeight: 1080 }
    ),
    true
  );

  const video = {
    getBoundingClientRect() {
      return {
        left: 0,
        top: 0,
        right: 1920,
        bottom: 1080,
        width: 1920,
        height: 1080,
      };
    },
  };
  const document = {
    fullscreenElement: null,
    webkitFullscreenElement: null,
    querySelector(selector) {
      return selector === "video" ? video : null;
    },
  };

  assert.equal(
    votingUi.isFullscreenActive(document, {
      innerWidth: 1920,
      innerHeight: 1080,
    }),
    true
  );
});

test("the Twitch notifications inbox is never treated as voting UI", async () => {
  const votingUi = await loadVotingUi();
  const notificationsDialog = {
    textContent:
      "Prediction Mark 239 as Read Notifications Settings My Twitch My Channel",
    closest(selector) {
      return selector.includes("onsite-notifications") ? this : null;
    },
    querySelectorAll() {
      return [{}, {}, {}];
    },
  };

  assert.equal(
    votingUi.isGenericVotingSurface(notificationsDialog, "prediction"),
    false
  );
});

test("the prediction bet prompt is never adopted as the voting banner", async () => {
  const votingUi = await loadVotingUi();
  // Twitch's points prompt looks like a voting surface: it says "prediction"
  // and is full of prediction-tagged buttons. Adopting it as the banner source
  // parks it off-screen, which is what stopped viewers placing a bet.
  const betPrompt = {
    textContent:
      "Prediction — How many Channel Points do you want to use? 1,000 5,000 10,000 Place bet",
    closest() {
      return null;
    },
    querySelectorAll() {
      return [{}, {}, {}, {}];
    },
  };

  assert.equal(votingUi.isPredictionBetPrompt(betPrompt), true);
  assert.equal(
    votingUi.isGenericVotingSurface(betPrompt, "prediction"),
    false
  );
});

test("a real prediction banner is still treated as voting UI", async () => {
  const votingUi = await loadVotingUi();
  const banner = {
    textContent: "Prediction Will the streamer win? Team A 1,234 Team B 567",
    closest() {
      return null;
    },
    querySelectorAll() {
      return [{}, {}];
    },
  };

  assert.equal(votingUi.isPredictionBetPrompt(banner), false);
  assert.equal(votingUi.isGenericVotingSurface(banner, "prediction"), true);
});
