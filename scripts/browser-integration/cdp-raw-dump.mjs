// Raw CDP client: enumerate all targets, open edge://extensions, dump developerPrivate.
import { writeFileSync } from "node:fs";

const port = process.argv[2] || "9333";
const out = process.argv[3] || process.env.TEMP + "\\chatterino-cdp-raw.json";

async function getJson(url) {
  const r = await fetch(url);
  return r.json();
}

const version = await getJson("http://127.0.0.1:" + port + "/json/version");
const wsUrl = version.webSocketDebuggerUrl;

const ws = new WebSocket(wsUrl);
let id = 0;
const pending = new Map();

ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const entry = pending.get(msg.id);
    pending.delete(msg.id);
    if (msg.error) entry.reject(new Error(JSON.stringify(msg.error)));
    else entry.resolve(msg.result);
  }
};
await new Promise((res, rej) => {
  ws.onopen = res;
  ws.onerror = rej;
});

function send(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const mid = ++id;
    pending.set(mid, { resolve, reject });
    const msg = { id: mid, method: method, params: params };
    if (sessionId) msg.sessionId = sessionId;
    ws.send(JSON.stringify(msg));
  });
}

const result = {};

// Enumerate targets
const t = await send("Target.getTargets");
result.targets = t.targetInfos.map((x) => ({ type: x.type, url: x.url, title: x.title }));

// Open edge://extensions via createTarget
let extTarget = null;
for (const url of ["edge://extensions", "chrome://extensions", "edge://extensions/"]) {
  try {
    const r = await send("Target.createTarget", { url: url });
    extTarget = { targetId: r.targetId, url: url };
    break;
  } catch (e) {
    result.createTargetErrors = result.createTargetErrors || [];
    result.createTargetErrors.push({ url: url, err: String(e) });
  }
}

if (extTarget) {
  await new Promise((r) => setTimeout(r, 8000));
  const t2 = await send("Target.getTargets");
  result.targetsAfter = t2.targetInfos.map((x) => ({ type: x.type, url: x.url, title: x.title }));
  const page = t2.targetInfos.find(
    (x) => x.url.startsWith("edge://extensions") || x.url.startsWith("chrome://extensions")
  );
  if (page) {
    const att = await send("Target.attachToTarget", { targetId: page.targetId, flatten: true });
    const sessionId = att.sessionId;
    const evalRes = await send(
      "Runtime.evaluate",
      {
        expression:
          "new Promise(r => { const dev = window.chrome && window.chrome.developerPrivate; " +
          "if (!dev) { r({ noDev: true, chrome: Object.keys(window.chrome || {}) }); return; } " +
          "dev.getExtensionsInfo({ includeDisabled: true, includeTerminated: true }, exts => { " +
          "r(exts.map(e => ({ id: e.id, name: e.name, state: e.state, location: e.location, " +
          "disableReasons: e.disableReasons, path: e.path, version: e.version, manifestErrors: e.manifestErrors }))); }); })",
        awaitPromise: true,
        returnByValue: true,
      },
      sessionId
    ).catch((e) => ({ error: String(e) }));
    result.extPage = { url: page.url, eval: evalRes };
    const bodyRes = await send(
      "Runtime.evaluate",
      { expression: "document.body ? document.body.innerText : ''", returnByValue: true },
      sessionId
    ).catch((e) => ({ error: String(e) }));
    result.bodyText = (bodyRes && bodyRes.result && bodyRes.result.value) || "";
  } else {
    result.extPageMissing = true;
  }
}

writeFileSync(out, JSON.stringify(result, null, 1));
console.log(
  JSON.stringify({
    wrote: out,
    targets: result.targets.length,
    extTargetOpened: extTarget ? extTarget.url : null,
    extPageReached: result.extPage ? result.extPage.url : null,
  })
);
ws.close();