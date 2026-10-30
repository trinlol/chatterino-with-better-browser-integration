import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";

const extensionRoot = new URL("../", import.meta.url);

test("the extension does not inject activity or moderation cards", async () => {
  const manifest = JSON.parse(
    await readFile(new URL("manifest.json", extensionRoot), "utf8")
  );
  const scripts = manifest.content_scripts.flatMap((entry) => entry.js || []);

  assert.equal(scripts.includes("activity-rail.js"), false);
  assert.equal(scripts.includes("moderator-cockpit.js"), false);

  const content = await readFile(new URL("content.js", extensionRoot), "utf8");
  const styles = await readFile(new URL("styles.css", extensionRoot), "utf8");
  assert.doesNotMatch(content, /renderProductSlices|ChatterinoActivityRail/);
  assert.doesNotMatch(styles, /chatterino-product-slice|chatterino-moderator/);
});

test("prediction fallback reopens the native bet prompt — no synthetic voting controls", async () => {
  const content = await readFile(new URL("content.js", extensionRoot), "utf8");
  const styles = await readFile(new URL("styles.css", extensionRoot), "utf8");

  // The fallback entry beside the player is allowed (it reopens Twitch's
  // native "How many Channel Points?" prompt via activateVotingTrigger or the
  // points replica). A synthetic GQL-built voting surface with its own
  // outcome buttons is not.
  assert.match(content, /handlePredictionFallback/);
  assert.doesNotMatch(styles, /chatterino-prediction-fallback-pill/);

  // The fallback must never carry its own outcome buttons — voting always
  // happens through Twitch's native control.
  assert.doesNotMatch(
    content,
    /chatterino-prediction-fallback[\s\S]*?role=["']radio["']/
  );
});

test("reward dialog and prediction dialog remain visible, interactive, and unclipped", async () => {
  const content = await readFile(new URL("content.js", extensionRoot), "utf8");
  const styles = await readFile(new URL("styles.css", extensionRoot), "utf8");
  const overlay = await readFile(new URL("overlay.js", extensionRoot), "utf8");

  // styles.css must unclip ancestors of both native dialogs
  assert.match(
    styles,
    /:has\(\.chatterino-native-reward-dialog,\s*\.chatterino-native-prediction-dialog\)/
  );

  // form controls inside both dialogs must be visible and interactive
  assert.match(
    styles,
    /\.chatterino-native-reward-dialog textarea[\s\S]*?visibility:\s*visible/
  );
  assert.match(
    styles,
    /\.chatterino-native-prediction-dialog textarea[\s\S]*?visibility:\s*visible/
  );

  // community-points-summary must remain visible when holding a mounted dialog
  assert.match(
    styles,
    /\[data-test-selector="community-points-summary"\]:has\([\s\S]*?\.chatterino-native-reward-dialog/
  );

  // content.js must scan for active native reward input banners and handle pending redemptions
  assert.match(content, /checkNativeRewardPending/);
  assert.match(content, /chat-input-reward-banner/);

  // overlay.js adjusts chat rect height when reward is pending to reveal the native input
  assert.match(overlay, /chatterino-reward-pending/);
  assert.match(overlay, /inputArea/);

  // styles.css must exclude ReactModal overlays and backdrops from dialog styling
  assert.match(styles, /ReactModal__Overlay\.chatterino-native-reward-dialog/);
  assert.match(styles, /pointer-events:\s*none\s*!important/);

  // content.js must guard against overlays and dismiss open dialogs on outside click
  assert.match(content, /isOverlayOrBackdrop/);
  assert.match(content, /dismissNativePredictionDialog/);
});
