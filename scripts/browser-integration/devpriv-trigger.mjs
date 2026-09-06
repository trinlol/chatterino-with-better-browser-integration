// Phase 1: launch fresh (about:blank), wait 45s -> snapshot (expect: no SW)
// Phase 2: open edge://extensions via CDP, wait 10s -> snapshot (expect: still no SW)
// Phase 3: attach to that page, call developerPrivate.getExtensionsInfo -> snapshot
// Phase 4: wait 15s -> snapshot (SW present? = devPriv query is the trigger)
import { writeFileSync } from "node:fs";

const port = "9333";
const out = process.env.TEMP + "\\chatterino-devpriv-trigger.json";

const version = await (await fetch("http://127.0.0.1:" + port + "/json/version")).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const e = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? e.reject(new Error(JSON.stringify(msg.error))) : e.resolve(msg.result);
  }
};
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
function send(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const mid = ++id;
    pending.set(mid, { resolve, reject });
    const m = { id: mid, method: method, params: params };
    if (sessionId) m.sessionId = sessionId;
    ws.send(JSON.stringify(m));
  });
}

async function snapshot(label) {
  const t = await send("Target.getTargets");
  const sw = t.targetInfos.filter((x) => x.url.includes("bogfpdfo")).map((x) => x.url);
  const pages = t.targetInfos.filter((x) => x.type === "page").map((x) => x.url);
  console.log(label + ": bogfpdfoSW=" + JSON.stringify(sw) + " pages=" + JSON.stringify(pages));
  return { sw: sw, pages: pages };
}

const result = {};
result.phase1 = await snapshot("phase1_launch+45s");

// Phase 2: open extensions page
await send("Target.createTarget", { url: "edge://extensions" });
await new Promise((r) => setTimeout(r, 10000));
result.phase2 = await snapshot("phase2_page_opened");

// Phase 3: call developerPrivate on that page
const t2 = await send("Target.getTargets");
const page = t2.targetInfos.find((x) => x.url.startsWith("edge://extensions"));
if (page) {
  const att = await send("Target.attachToTarget", { targetId: page.targetId, flatten: true });
  const ev = await send(
    "Runtime.evaluate",
    {
      expression:
        "new Promise(r => { const dev = window.chrome && window.chrome.developerPrivate; " +
        "if (!dev) { r('noDev'); return; } " +
        "dev.getExtensionsInfo({ includeDisabled: true, includeTerminated: true }, exts => r(exts.length + ' extensions')); })",
      awaitPromise: true,
      returnByValue: true,
    },
    att.sessionId
  ).catch((e) => ({ error: String(e) }));
  result.devPrivResult = ev?.result?.value || ev;
  console.log("devPriv: " + result.devPrivResult);
}

await new Promise((r) => setTimeout(r, 15000));
result.phase4 = await snapshot("phase4_after_devpriv_15s");

writeFileSync(out, JSON.stringify(result, null, 1));
ws.close();