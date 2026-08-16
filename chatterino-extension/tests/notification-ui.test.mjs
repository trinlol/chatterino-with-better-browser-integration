import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadNotificationUi() {
  const source = await readFile(
    new URL("../notification-ui.js", import.meta.url),
    "utf8"
  );
  const window = {};
  vm.runInNewContext(source, { window });
  return window.ChatterinoNotificationUi;
}

test("recognises Twitch's stable notifications toggle", async () => {
  const notificationUi = await loadNotificationUi();
  const button = {
    getAttribute(name) {
      return name === "aria-label" ? "Open Notifications" : null;
    },
    closest(selector) {
      if (selector === "button") return this;
      if (selector.includes("onsite-notifications")) return {};
      return null;
    },
  };

  assert.equal(notificationUi.findToggle(button), button);
  assert.equal(notificationUi.toggleWillOpen(button), true);
});

test("detects the notification balloon host after Twitch fills it", async () => {
  const notificationUi = await loadNotificationUi();
  const balloonHost = { childElementCount: 1, textContent: "My Twitch" };
  const detector = { parentElement: { children: [null, balloonHost] } };
  detector.parentElement.children[0] = detector;
  const root = {
    querySelector(selector) {
      if (selector.includes("Close Notifications")) return null;
      if (selector.includes("aria-expanded")) return null;
      if (selector.includes("toggle-balloon-wrapper")) return detector;
      if (selector.includes('[role="dialog"]')) return null;
      return null;
    },
  };
  const document = {
    querySelector(selector) {
      return selector.includes("onsite-notifications") ? root : null;
    },
  };

  assert.equal(notificationUi.isOpen(document), true);
});

test("moves the notification panel only far enough left to clear chat", async () => {
  const notificationUi = await loadNotificationUi();
  const properties = new Map();
  let renderedRight = 1900;
  const panel = {
    dataset: {},
    getBoundingClientRect() {
      return { right: renderedRight };
    },
    style: {
      setProperty(name, value, priority) {
        properties.set(name, { value, priority });
      },
      removeProperty(name) {
        properties.delete(name);
      },
    },
  };

  assert.equal(
    notificationUi.positionPanelLeftOf(panel, { left: 1580 }, 12),
    332
  );
  assert.deepEqual(properties.get("translate"), {
    value: "-332px 0px",
    priority: "important",
  });

  // getBoundingClientRect includes the translation we applied. Re-running
  // positioning after a Twitch mutation must retain, not compound, the shift.
  renderedRight = 1568;
  assert.equal(
    notificationUi.positionPanelLeftOf(panel, { left: 1580 }, 12),
    332
  );

  notificationUi.clearPanelShift(panel);
  assert.equal(properties.has("translate"), false);
  assert.equal(panel.dataset.ccNotificationShift, undefined);
});

test("overlay detaches for the inbox and no longer collapses chat from top-nav clicks", async () => {
  const overlaySource = await readFile(
    new URL("../overlay.js", import.meta.url),
    "utf8"
  );

  assert.match(overlaySource, /ChatterinoNotificationUi/);
  assert.match(overlaySource, /positionPanelLeftOf/);
  assert.match(overlaySource, /type:\s*["']detach["']/);
  assert.doesNotMatch(
    overlaySource,
    /findNavBar\(\)[\s\S]{0,500}addEventListener\(["']mouseup["'][\s\S]{0,500}findRightCollapse\(\)\.click\(\)/
  );
});
