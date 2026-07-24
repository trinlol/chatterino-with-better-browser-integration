import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

class FakeElement {
  constructor({ interactive = false } = {}) {
    this.children = [];
    this.parentElement = null;
    this.interactive = interactive;
    this.clicked = 0;
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
