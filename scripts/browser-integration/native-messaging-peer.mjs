import { spawn } from "node:child_process";
import { once } from "node:events";
import path from "node:path";
import { fileURLToPath } from "node:url";

// Chrome native messaging uses a little-endian uint32 length followed by UTF-8
// JSON. Keep the fake peer strict so a malformed fixture cannot turn a CI test
// into an unbounded allocation.
export const MAX_NATIVE_MESSAGE_BYTES = 1024 * 1024;

export function encodeNativeMessage(message) {
  const payload = Buffer.from(JSON.stringify(message), "utf8");
  if (payload.length > MAX_NATIVE_MESSAGE_BYTES) {
    throw new RangeError(
      `native message exceeds ${MAX_NATIVE_MESSAGE_BYTES} bytes`
    );
  }

  const frame = Buffer.allocUnsafe(4 + payload.length);
  frame.writeUInt32LE(payload.length, 0);
  payload.copy(frame, 4);
  return frame;
}

export function createNativeMessageDecoder(onMessage) {
  let buffered = Buffer.alloc(0);

  return (chunk) => {
    buffered = Buffer.concat([buffered, chunk]);
    while (buffered.length >= 4) {
      const length = buffered.readUInt32LE(0);
      if (length > MAX_NATIVE_MESSAGE_BYTES) {
        throw new RangeError(
          `native message exceeds ${MAX_NATIVE_MESSAGE_BYTES} bytes`
        );
      }
      if (buffered.length < 4 + length) return;

      const payload = buffered.subarray(4, 4 + length).toString("utf8");
      buffered = buffered.subarray(4 + length);
      onMessage(JSON.parse(payload));
    }
  };
}

export async function startFakeNativeHost(scenario, options = {}) {
  const script = path.join(
    path.dirname(fileURLToPath(import.meta.url)),
    "native-messaging-fake-host.mjs"
  );
  const child = spawn(process.execPath, [script], {
    cwd: options.cwd,
    env: {
      ...process.env,
      CHATTERINO_FAKE_HOST_SCENARIO: JSON.stringify(scenario),
    },
    stdio: ["pipe", "pipe", "pipe"],
    windowsHide: true,
  });
  const received = [];
  const stderr = [];
  const decode = createNativeMessageDecoder((message) =>
    received.push(message)
  );
  child.stdout.on("data", decode);
  child.stderr.on("data", (chunk) => stderr.push(chunk.toString("utf8")));

  const exit = once(child, "exit").then(([code, signal]) => ({ code, signal }));
  return {
    child,
    received,
    get stderr() {
      return stderr.join("");
    },
    exit,
    send(message) {
      if (!child.stdin.writable)
        throw new Error("fake native host is not writable");
      child.stdin.write(encodeNativeMessage(message));
    },
    async waitFor(predicate, timeoutMs = 1000) {
      const started = Date.now();
      while (Date.now() - started < timeoutMs) {
        const match = received.find(predicate);
        if (match) return match;
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
      throw new Error(
        `timed out after ${timeoutMs}ms; frames=${JSON.stringify(received)}`
      );
    },
    close() {
      child.stdin.end();
    },
  };
}
