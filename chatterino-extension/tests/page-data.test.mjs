import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadPageData() {
  const source = await readFile(
    new URL("../page-data.js", import.meta.url),
    "utf8"
  );
  const window = {};
  vm.runInNewContext(source, { window });
  return window.ChatterinoPageData;
}

test("a prediction attribute keeps its fields and drops prototype keys", async () => {
  const pageData = await loadPageData();
  const parsed = pageData.parseActivityAttribute(
    JSON.stringify({
      title: "Will we win?",
      options: [{ title: "Yes" }, { title: "No" }],
      status: "started",
      closesAt: 1767225600000,
    }).replace("{", '{"__proto__":{"polluted":true},')
  );
  assert.equal(parsed.title, "Will we win?");
  assert.equal(parsed.options.length, 2);
  assert.equal(parsed.closesAt, 1767225600000);
  assert.equal(Object.getPrototypeOf(parsed).polluted, undefined);
  assert.equal(Object.hasOwn(parsed, "__proto__"), false);
});

test("an activity without a usable title is discarded", async () => {
  const pageData = await loadPageData();
  assert.equal(pageData.parseActivityAttribute(""), null);
  assert.equal(pageData.parseActivityAttribute('{"title":42}'), null);
  assert.equal(pageData.parseActivityAttribute('{"title":"  "}'), null);
  assert.equal(pageData.parseActivityAttribute("[1,2]"), null);
});

test("a bare-string activity attribute is still accepted as a title", async () => {
  const pageData = await loadPageData();
  const parsed = pageData.parseActivityAttribute("Next map?");
  assert.equal(parsed.title, "Next map?");
  assert.equal(parsed.status, "started");
});

test("oversized and deeply nested page data is bounded", async () => {
  const pageData = await loadPageData();
  const { LIMITS } = pageData;
  const clean = pageData.sanitizeJson({
    long: "x".repeat(LIMITS.stringLength + 50),
    list: Array.from({ length: LIMITS.arrayLength + 10 }, (_, i) => i),
    deep: { a: { b: { c: { d: { e: { f: { g: 1 } } } } } } },
    nan: Number.NaN,
    fn: () => 1,
  });
  assert.equal(clean.long.length, LIMITS.stringLength);
  assert.equal(clean.list.length, LIMITS.arrayLength);
  assert.equal(clean.nan, null);
  assert.equal("fn" in clean, false);
  assert.equal(clean.deep.a.b.c.d.e, undefined);
});

test("balances and ids only accept the formats Twitch uses", async () => {
  const pageData = await loadPageData();
  assert.equal(pageData.sanitizeBalance("1,240"), "1,240");
  assert.equal(pageData.sanitizeBalance("12.3K"), "12.3K");
  assert.equal(pageData.sanitizeBalance(1290), "1290");
  assert.equal(pageData.sanitizeBalance("<img src=x>"), "");
  assert.equal(pageData.sanitizeId("a1b2-c3_d4"), "a1b2-c3_d4");
  assert.equal(pageData.sanitizeId("bad id"), "");
});

test("a malformed event snapshot is rejected and a valid one normalised", async () => {
  const pageData = await loadPageData();
  assert.equal(pageData.sanitizeSnapshot("nope"), null);
  assert.equal(pageData.sanitizeSnapshot(null), null);

  const snapshot = pageData.sanitizeSnapshot({
    channelPoints: { balance: "1,240", claimAvailable: "yes", claimId: "abc" },
    prediction: { title: "Win?", options: "Yes,No" },
    poll: { options: ["A"] },
    rewards: [{ id: "r1" }, "junk", null],
    lastUpdate: 5,
  });
  assert.equal(snapshot.channelPoints.balance, "1,240");
  assert.equal(snapshot.channelPoints.claimAvailable, false);
  assert.equal(snapshot.prediction.title, "Win?");
  assert.deepEqual([...snapshot.prediction.options], []);
  assert.equal(snapshot.poll, null);
  assert.equal(snapshot.rewards.length, 1);
  assert.equal(snapshot.lastUpdate, 5);
});

test("ended DOM activities expire after the grace period; anything else resets it", async () => {
  const pageData = await loadPageData();
  let now = 1_000;
  const expiry = pageData.createEndedActivityExpiry({ ttlMs: 30_000, now: () => now });
  const ended = { title: "Next map?", options: ["A", "B"], status: "ended" };
  assert.equal(expiry.filter("poll", ended), ended);
  now += 29_999;
  assert.equal(expiry.filter("poll", { ...ended }).title, "Next map?");
  now += 1;
  assert.equal(expiry.filter("poll", { ...ended }), null);
  // Kinds are tracked separately and started activities pass through.
  assert.equal(expiry.filter("prediction", ended), ended);
  const started = { ...ended, status: "started" };
  assert.equal(expiry.filter("poll", started), started);
  assert.equal(expiry.filter("poll", { ...ended }).status, "ended");
  // A different ended poll gets its own grace period; reset clears all.
  now += 40_000;
  assert.equal(expiry.filter("poll", { ...ended }), null);
  assert.ok(expiry.filter("poll", { ...ended, title: "Other" }));
  expiry.reset();
  assert.ok(expiry.filter("poll", { ...ended }));
  assert.equal(expiry.filter("poll", null), null);
});
