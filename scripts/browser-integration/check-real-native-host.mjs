// Exercises the built native executable, with isolated IPC/settings directories.
// Usage: node scripts/browser-integration/check-real-native-host.mjs <executable>
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import {
  createNativeMessageDecoder,
  encodeNativeMessage,
} from "./native-messaging-peer.mjs";

const executable = path.resolve(process.argv[2] || "build/bin/Chatterino Better Browser.exe");

async function checkHost() {
  const profile = await mkdtemp(path.join(os.tmpdir(), "chatterino-real-host-"));
  const child = spawn(executable, [
    "--portable", "--portable-dir", profile,
    "chrome-extension://bogfpdfoagkaebimmlcbgmfmanhbhhlm/",
  ], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
  const frames = [];
  let stderr = "";
  let failure = null;
  let readyTimer;
  let eofTimer;
  const completed = new Promise((resolve, reject) => {
    child.on("error", reject);
    child.on("close", (code, signal) => resolve({ code, signal }));
  });
  const decode = createNativeMessageDecoder((frame) => {
    frames.push(frame);
    if (frame.status === "native-host-ready") {
      clearTimeout(readyTimer);
      // Harmless input to exercise forwarding; there is no desktop in this
      // temporary profile. EOF must still close the host cleanly.
      child.stdin.write(encodeNativeMessage({ action: "sync", twitchChannels: [] }));
      eofTimer = setTimeout(() => child.stdin.end(), 100);
    }
  });
  child.stdin.on("error", (error) => { failure ||= error; });
  child.stdout.on("data", (chunk) => {
    try { decode(chunk); } catch (error) { failure = error; child.kill(); }
  });
  child.stderr.on("data", (chunk) => { stderr += chunk.toString(); });
  readyTimer = setTimeout(() => {
    failure = new Error("native readiness timed out");
    child.kill();
  }, 5000);
  const exitTimer = setTimeout(() => {
    failure = new Error("native host did not exit after EOF");
    child.kill();
  }, 8000);
  try {
    const result = await completed;
    if (failure) throw failure;
    assert.equal(result.code, 0, `host exit ${result.code}: ${stderr}`);
    assert.equal(result.signal, null);
    assert.equal(frames.filter(f => f.status === "native-host-ready").length, 1);
    assert.equal(frames.at(-1)?.status, "exiting-host");
    return { pid: child.pid, profile, frames: frames.map(f => f.status), exitCode: result.code };
  } finally {
    clearTimeout(readyTimer);
    clearTimeout(eofTimer);
    clearTimeout(exitTimer);
  }
}

// Overlap startup to catch shared-log contention; repeat to catch reload/EOF
// regressions. Every host gets a distinct IPC directory and cannot steal live
// browser acknowledgements.
const results = [];
for (let round = 0; round < 3; round++) {
  results.push(...await Promise.all([checkHost(), checkHost()]));
}
console.log(JSON.stringify({ passed: results.length, executable, results }, null, 2));
