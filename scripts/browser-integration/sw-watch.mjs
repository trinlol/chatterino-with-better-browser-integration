// Attach to the bogfpdfo service worker and record its console for 90s.
import { writeFileSync } from "node:fs";

const port = "9333";
const out = process.env.TEMP + "\\chatterino-sw-console.json";
const events = [];

const version = await (await fetch("http://127.0.0.1:" + port + "/json/version")).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const sessions = new Set();

ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const e = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? e.reject(new Error(JSON.stringify(msg.error))) : e.resolve(msg.result);
    return;
  }
  // events
  if (msg.method === "Target.attachedToTarget") {
    sessions.add(msg.params.sessionId);
    events.push({ t: Date.now(), ev: "attached", target: msg.params.targetInfo.url });
    // enable console + runtime on the new session
    for (const m of ["Runtime.enable", "Log.enable"]) {
      ws.send(JSON.stringify({ id: ++id, method: m, params: {}, sessionId: msg.params.sessionId }));
    }
  }
  if (msg.method === "Runtime.consoleAPICalled") {
    events.push({
      t: Date.now(), ev: "console",
      type: msg.params.type,
      text: (msg.params.args || []).map((a) => a.value ?? a.description ?? "").join(" ").slice(0, 400),
    });
  }
  if (msg.method === "Runtime.exceptionThrown") {
    events.push({
      t: Date.now(), ev: "exception",
      text: (msg.params.exceptionDetails?.exception?.description || msg.params.exceptionDetails?.text || "").slice(0, 400),
    });
  }
  if (msg.method === "Target.targetCreated" || msg.method === "Target.targetDestroyed") {
    events.push({ t: Date.now(), ev: msg.method, target: msg.params.targetInfo?.url });
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

await send("Target.setAutoAttach", {
  autoAttach: true,
  waitForDebuggerOnStart: false,
  flatten: true,
});

console.log("watching for 90s...");
await new Promise((r) => setTimeout(r, 90000));
writeFileSync(out, JSON.stringify(events, null, 1));
console.log("events: " + events.length + " -> " + out);
ws.close();