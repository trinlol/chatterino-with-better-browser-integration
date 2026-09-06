import {
  createNativeMessageDecoder,
  encodeNativeMessage,
} from "./native-messaging-peer.mjs";

function parseScenario() {
  try {
    const value = JSON.parse(process.env.CHATTERINO_FAKE_HOST_SCENARIO || "{}");
    if (!value || typeof value !== "object")
      throw new TypeError("must be an object");
    return value;
  } catch (error) {
    process.stderr.write(
      `invalid CHATTERINO_FAKE_HOST_SCENARIO: ${error.message}\n`
    );
    process.exit(64);
  }
}

const scenario = parseScenario();
let inboundCount = 0;

function emit(message, delayMs = 0) {
  setTimeout(
    () => {
      process.stdout.write(encodeNativeMessage(message));
    },
    Math.max(0, delayMs)
  );
}

function matches(rule, message) {
  return Object.entries(rule || {}).every(
    ([key, value]) => message[key] === value
  );
}

for (const item of scenario.startup ?? []) {
  emit(item.message, item.delayMs);
}

process.stdin.on(
  "data",
  createNativeMessageDecoder((message) => {
    inboundCount += 1;
    for (const behavior of scenario.onInbound ?? []) {
      if (!matches(behavior.match, message)) continue;
      const frames = [...(behavior.frames ?? [])];
      if (behavior.order === "reverse") frames.reverse();
      for (const frame of frames) {
        const payload = frame.echoAttachRequestId
          ? { ...frame.message, attachRequestId: message.attachRequestId }
          : frame.message;
        emit(payload, frame.delayMs);
      }
      if (behavior.disconnect) {
        const delay =
          Math.max(...frames.map((frame) => frame.delayMs || 0), 0) + 1;
        setTimeout(() => process.exit(0), delay);
      }
    }
    if (scenario.disconnectOnInbound === inboundCount) process.exit(0);
  })
);

process.stdin.on("end", () => process.exit(0));
