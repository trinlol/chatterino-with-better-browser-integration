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
