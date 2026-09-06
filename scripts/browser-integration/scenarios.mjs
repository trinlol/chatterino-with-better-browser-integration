const matchingAck = {
  type: "status",
  status: "chat-attached",
  winId: "42",
};

export const lifecycleScenarios = {
  "browser-first": {
    startup: [{ message: { type: "status", status: "native-host-ready" } }],
    onInbound: [
      {
        match: { action: "select" },
        frames: [{ message: matchingAck, echoAttachRequestId: true }],
      },
    ],
  },
  "desktop-first": {
    startup: [{ message: { type: "status", status: "desktop-ready" } }],
    onInbound: [
      {
        match: { action: "select" },
        frames: [{ message: matchingAck, echoAttachRequestId: true }],
      },
    ],
  },
  "stale-ack": {
    onInbound: [
      {
        match: { action: "select" },
        frames: [
          { message: matchingAck, echoAttachRequestId: true, delayMs: 1 },
          { message: { ...matchingAck, attachRequestId: "stale-request" } },
        ],
        order: "reverse",
      },
    ],
  },
  "host-death": { disconnectOnInbound: 1 },
  navigation: {
    onInbound: [
      {
        match: { action: "detach" },
        frames: [{ message: { type: "status", status: "chat-detached" } }],
      },
    ],
  },
  "worker-recreation": {
    startup: [{ message: { type: "status", status: "native-host-ready" } }],
    onInbound: [
      {
        match: { action: "select" },
        frames: [{ message: matchingAck, echoAttachRequestId: true }],
      },
    ],
  },
  "two-windows-same-channel": {
    onInbound: [
      {
        match: { action: "select" },
        frames: [{ message: matchingAck, echoAttachRequestId: true }],
      },
    ],
  },
  "fail-open-timing": {
    onInbound: [
      {
        match: { action: "select" },
        frames: [
          {
            message: {
              type: "status",
              status: "chat-rejected",
              reason: "fake-host-reject",
            },
            delayMs: 50,
          },
        ],
      },
    ],
  },
};
