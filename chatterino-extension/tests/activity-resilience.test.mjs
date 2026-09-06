import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadActivity() {
  const window = {};
  const source = await readFile(
    new URL("../activity-state.js", import.meta.url),
    "utf8"
  );
  vm.runInNewContext(source, {
    window,
    Date,
    Math,
    Number,
    String,
    JSON,
    Object,
    Array,
  });
  return window.ChatterinoActivity;
}

test("stale or unsupported GraphQL activity expires before the DOM fallback", async () => {
  const { ActivityStore } = await loadActivity();
  let now = 1_700_000_000_000;
  const store = new ActivityStore(() => now);
  store.setChannel("example");
  store.observeDom("poll", { title: "DOM poll", options: ["A", "B"] });
  store.applyGraphql({
    poll: { title: "GraphQL poll", options: ["One", "Two"] },
    adapter: {
      source: "private-graphql",
      updatedAt: 1_700_000_000_000,
      freshUntil: 1_700_000_000_050,
      supported: true,
    },
  });

  assert.equal(store.current("poll").title, "GraphQL poll");
  now = 1_700_000_000_051;
  assert.equal(store.current("poll").title, "DOM poll");
  assert.equal(store.snapshot().adapter.source, "private-graphql");

  store.applyGraphql({
    poll: { title: "Unavailable GraphQL poll" },
    adapter: { source: "private-graphql", supported: false, failure: "schema" },
  });
  assert.equal(store.current("poll").title, "DOM poll");
  assert.equal(store.snapshot().adapter.failure, "schema");
});

test("bounded metrics record callback work and allow fixture p95 comparisons", async () => {
  const { ActivityStore, PerformanceMetrics } = await loadActivity();
  const metrics = new PerformanceMetrics();
  for (let index = 0; index < 140; index += 1) metrics.recordCallback(index);
  const snapshot = metrics.snapshot();
  assert.equal(snapshot.callbackCount, 140);
  assert.equal(snapshot.callbackDurationSamples, 128);
  assert.equal(snapshot.callbackDurationP95, 133);
  assert.equal(
    PerformanceMetrics.isP95WithinBaseline(
      { callbackDurationP95: 100 },
      { callbackDurationP95: 110 }
    ),
    true
  );
  assert.equal(
    PerformanceMetrics.isP95WithinBaseline(
      { callbackDurationP95: 100 },
      { callbackDurationP95: 111 }
    ),
    false
  );

  const store = new ActivityStore(() => 10, metrics);
  store.setChannel("example");
  store.recordObserverCallback(4);
  store.observeDom("prediction", {
    title: "Will we win?",
    options: ["Yes", "No"],
  });
  assert.equal(store.nextPublication("prediction").lifecycle, "upsert");
  assert.equal(store.nextPublication("prediction"), null);
  assert.equal(store.snapshot().metrics.activityPublications, 1);
  assert.equal(store.snapshot().metrics.callbackCount, 141);
});
