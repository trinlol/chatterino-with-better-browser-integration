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

test("GraphQL prediction metadata cannot create a synthetic voting button", async () => {
  const content = await readFile(new URL("content.js", extensionRoot), "utf8");
  const styles = await readFile(new URL("styles.css", extensionRoot), "utf8");

  assert.doesNotMatch(
    content,
    /handlePredictionFallback|activateVotingTrigger/
  );
  assert.doesNotMatch(styles, /chatterino-prediction-fallback-pill/);
});
