import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadModule() {
  const context = {};
  context.globalThis = context;
  vm.runInNewContext(
    await readFile(
      new URL("../integration-health.js", import.meta.url),
      "utf8"
    ),
    context
  );
  return context.ChatterinoIntegrationHealth;
}

test("health summary distinguishes a blocked host from an idle tab", async () => {
  const { summarize } = await loadModule();
  assert.equal(
    summarize({ native: { connected: false, blocked: true } }).headline,
    "Native host blocked"
  );
  assert.equal(
    summarize({ native: { connected: true }, tab: null }).headline,
    "No active Twitch channel"
  );
});

test("healthy content reports both Activity slots", async () => {
  const { summarize } = await loadModule();
  const summary = summarize({
    native: { connected: true },
    tab: { channel: "example" },
    content: {
      channel: "example",
      companionActive: true,
      activities: {
        poll: { title: "Best map?" },
        prediction: { title: "Will we win?" },
      },
    },
  });
  assert.equal(summary.level, "ok");
  assert.ok(summary.lines.includes("Poll: Best map?"));
  assert.ok(summary.lines.includes("Prediction: Will we win?"));
});
