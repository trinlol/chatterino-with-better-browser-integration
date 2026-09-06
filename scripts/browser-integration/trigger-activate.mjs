// Test: does ACTIVATING an edge://extensions tab trigger the unpacked load?
import { writeFileSync } from "node:fs";

const port = "9333";
const out = process.env.TEMP + "\\chatterino-activate-test.json";

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

async function snapshot(label) {
  const t = await send("Target.getTargets");
  const sw = t.targetInfos.filter((x) => x.url.includes("bogfpdfo")).map((x) => x.url);
  console.log(label + ": bogfpdfoSW=" + JSON.stringify(sw));
  return sw;
}

const result = {};
result.before = await snapshot("before");

// Create the extensions tab and ACTIVATE it (bring to foreground)
const created = await send("Target.createTarget", { url: "edge://extensions" });
await send("Target.activateTarget", { targetId: created.targetId });

for (const wait of [5000, 15000, 30000]) {
  await new Promise((r) => setTimeout(r, wait === 5000 ? 5000 : wait === 15000 ? 10000 : 15000));
  const sw = await snapshot("t+" + wait + "ms");
  if (sw.length > 0) break;
}

writeFileSync(out, JSON.stringify(result, null, 1));
ws.close();