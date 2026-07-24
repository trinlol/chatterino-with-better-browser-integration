import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadModule() {
  const window = {};
  const source = await readFile(
    new URL("../activity-state.js", import.meta.url),
    "utf8"
  );
  vm.runInNewContext(source, { window });
  return window.ChatterinoActivity;
}

test("GraphQL enriches DOM activity without replacing its available fields", async () => {
  const { mergeActivities } = await loadModule();
  const activity = mergeActivities(
    "poll",
    { title: "Best map?", options: ["Dust II", "Mirage"], durationSeconds: 90 },
    { title: "Best map?", status: "started" }
  );

  assert.deepEqual([...activity.options], ["Dust II", "Mirage"]);
  assert.equal(activity.durationSeconds, 90);
  assert.equal(activity.source, "dom+graphql");
});

test("poll and prediction publications are independent and removals are explicit", async () => {
  const { ActivityStore } = await loadModule();
  const store = new ActivityStore();
  store.setChannel("Example");
  store.applyGraphql({
    poll: { title: "Best map?", options: ["A", "B"] },
    prediction: { title: "Will we win?", options: ["Yes", "No"] },
  });

  assert.equal(store.nextPublication("poll").lifecycle, "upsert");
  assert.equal(store.nextPublication("prediction").lifecycle, "upsert");
  assert.equal(store.nextPublication("poll"), null);

  store.applyGraphql({
    poll: null,
    prediction: { title: "Will we win?", options: ["Yes", "No"] },
  });

  assert.equal(store.nextPublication("poll").lifecycle, "remove");
  assert.equal(store.nextPublication("prediction"), null);
});

test("changing channel clears Adapter state and republishes independently", async () => {
  const { ActivityStore } = await loadModule();
  const store = new ActivityStore();
  store.setChannel("one");
  store.observeDom("prediction", { title: "First event" });
  assert.equal(store.nextPublication("prediction").lifecycle, "upsert");

  assert.equal(store.setChannel("two"), true);
  assert.equal(store.snapshot().prediction, null);
  assert.equal(store.nextPublication("prediction"), null);
});
