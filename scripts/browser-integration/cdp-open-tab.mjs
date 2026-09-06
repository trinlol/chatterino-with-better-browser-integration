// Open a tab (default about:blank) via raw CDP createTarget.
const port = process.argv[2] || "9333";
const url = process.argv[3] || "about:blank";
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
const r = await new Promise((resolve, reject) => {
  const mid = ++id;
  pending.set(mid, { resolve, reject });
  ws.send(JSON.stringify({ id: mid, method: "Target.createTarget", params: { url: url } }));
});
console.log(JSON.stringify({ created: r.targetId, url: url }));
ws.close();