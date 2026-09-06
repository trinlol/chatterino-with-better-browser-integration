// Discriminate WHEN the unpacked extension loads:
//  t0: launch (about:blank only, no extensions page)
//  t+60s: snapshot targets + host logs  (pre-trigger baseline)
//  then: open edge://extensions via createTarget
//  t+75s: snapshot targets + host logs again (post-trigger)
import { writeFileSync } from "node:fs";

const port = "9333";
const out = process.env.TEMP + "\\chatterino-trigger-test.json";

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
function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const mid = ++id;
    pending.set(mid, { resolve, reject });
    ws.send(JSON.stringify({ id: mid, method: method, params: params }));
  });
}

async function snapshot() {
  const t = await send("Target.getTargets");
  const sw = t.targetInfos.filter((x) => x.url.includes("bogfpdfo"));
  const pages = t.targetInfos.filter((x) => x.type === "page").map((x) => x.url);
  return { bogfpdfoSW: sw.map((x) => x.url), pages: pages };
}

const result = {};
result.t60_beforePage = await snapshot();          // 60s after launch, no extensions page yet
await send("Target.createTarget", { url: "edge://extensions" });
await new Promise((r) => setTimeout(r, 15000));
result.t75_afterPage = await snapshot();           // after opening extensions page

writeFileSync(out, JSON.stringify(result, null, 1));
console.log(JSON.stringify(result, null, 1));
ws.close();