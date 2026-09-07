import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

async function loadLogConsole() {
  const context = {
    Object,
    Array,
    Map,
    Set,
    Date,
    JSON,
    Number,
    String,
    Boolean,
    Math,
  };
  context.window = context;
  context.globalThis = context;
  const source = await readFile(
    new URL("../log-console.js", import.meta.url),
    "utf8"
  );
  vm.runInNewContext(source, context);
  return context.ChatterinoLogConsole;
}

test("ChunkReassembler reassembles in-order chunks", async () => {
  const { ChunkReassembler } = await loadLogConsole();
  const reassembler = new ChunkReassembler();
  const data = { hello: "world", count: 42 };
  const jsonStr = JSON.stringify(data);
  const part1 = jsonStr.slice(0, 10);
  const part2 = jsonStr.slice(10);

  const res0 = reassembler.processChunk({
    requestId: "req-1",
    seq: 0,
    total: 2,
    data: part1,
  });
  assert.equal(res0, null);

  const res1 = reassembler.processChunk({
    requestId: "req-1",
    seq: 1,
    total: 2,
    data: part2,
  });
  assert.deepEqual(res1, data);
});

test("ChunkReassembler reassembles out-of-order chunks", async () => {
  const { ChunkReassembler } = await loadLogConsole();
  const reassembler = new ChunkReassembler();
  const data = { stream: "online", viewers: 9999 };
  const jsonStr = JSON.stringify(data);
  const chunkLen = Math.ceil(jsonStr.length / 3);
  const part0 = jsonStr.slice(0, chunkLen);
  const part1 = jsonStr.slice(chunkLen, chunkLen * 2);
  const part2 = jsonStr.slice(chunkLen * 2);

  // Send part 2 first
  assert.equal(
    reassembler.processChunk({
      requestId: "req-2",
      seq: 2,
      total: 3,
      data: part2,
    }),
    null
  );

  // Send part 0 second
  assert.equal(
    reassembler.processChunk({
      requestId: "req-2",
      seq: 0,
      total: 3,
      data: part0,
    }),
    null
  );

  // Send part 1 last
  const res = reassembler.processChunk({
    requestId: "req-2",
    seq: 1,
    total: 3,
    data: part1,
  });
  assert.deepEqual(res, data);
});

test("ChunkReassembler returns null on missing chunks", async () => {
  const { ChunkReassembler } = await loadLogConsole();
  const reassembler = new ChunkReassembler();

  assert.equal(
    reassembler.processChunk({
      requestId: "req-missing",
      seq: 0,
      total: 3,
      data: '{"key":',
    }),
    null
  );

  assert.equal(
    reassembler.processChunk({
      requestId: "req-missing",
      seq: 2,
      total: 3,
      data: '"val"}',
    }),
    null
  );

  // seq 1 never arrived; another request shouldn't interfere
  assert.deepEqual(
    reassembler.processChunk({
      requestId: "req-other",
      seq: 0,
      total: 1,
      data: '{"ok":true}',
    }),
    { ok: true }
  );
});

test("ChunkReassembler discards chunks older than 5000ms", async () => {
  const { ChunkReassembler } = await loadLogConsole();
  const reassembler = new ChunkReassembler();

  // Arrival at t = 1000
  assert.equal(
    reassembler.processChunk(
      { requestId: "req-timeout", seq: 0, total: 2, data: '{"a":' },
      1000
    ),
    null
  );

  // Next chunk arrives at t = 6001 (> 5000ms later)
  // The old entry should be purged and this chunk treated as a fresh/orphaned sequence
  const res = reassembler.processChunk(
    { requestId: "req-timeout", seq: 1, total: 2, data: '1}' },
    6001
  );
  assert.equal(res, null);

  // Clean timeouts check
  reassembler.cleanTimeouts(20000);
  assert.equal(reassembler.requests.size, 0);
});

test("mergeLogData shapes native + local correctly", async () => {
  const { mergeLogData } = await loadLogConsole();

  const nativeSnapshot = {
    channel: { name: "testchannel", roomId: "12345" },
    stream: {
      live: true,
      title: "Playing a Game",
      game: "Chess",
      viewerCount: 1500,
      uptimeSeconds: 7200,
      streamType: "live",
    },
    modes: {
      submode: true,
      emoteOnly: false,
      r9k: true,
      slowMode: 30,
      followerOnly: 10,
    },
    self: {
      subscribed: true,
      mod: false,
      vip: true,
      broadcaster: false,
    },
    chat: {
      chatterCount: 350,
      messageCount: 1000,
      lastMessage: { user: "alice", text: "pog" },
    },
  };

  const localState = {
    sessionStore: {
      "browser:123": { windowId: 1, tabId: 2, channel: "testchannel", attached: true },
    },
    transitions: [{ timestamp: 1234567, event: "connect" }],
    nativeSupportsV2: true,
    lastNativeError: "",
    nativeConnectedSince: 1000000,
    badgeState: "normal",
  };

  const merged = mergeLogData(nativeSnapshot, localState);

  assert.equal(merged.session.connectedSince, 1000000);
  assert.equal(merged.session.nativeSupportsV2, true);
  assert.equal(merged.session.lastError, null);
  assert.equal(merged.session.badgeState, "normal");
  assert.deepEqual(merged.session.sessionStore, localState.sessionStore);

  assert.equal(merged.channel.name, "testchannel");
  assert.equal(merged.channel.roomId, "12345");
  assert.equal(merged.channel.live, true);
  assert.equal(merged.channel.title, "Playing a Game");
  assert.equal(merged.channel.game, "Chess");
  assert.equal(merged.channel.viewerCount, 1500);
  assert.equal(merged.channel.uptimeSeconds, 7200);

  assert.equal(merged.chatAndModes.submode, true);
  assert.equal(merged.chatAndModes.emoteOnly, false);
  assert.equal(merged.chatAndModes.r9k, true);
  assert.equal(merged.chatAndModes.slowMode, 30);
  assert.equal(merged.chatAndModes.followerOnly, 10);
  assert.deepEqual({ ...merged.chatAndModes.self }, {
    subscribed: true,
    mod: false,
    vip: true,
    broadcaster: false,
  });
  assert.equal(merged.chatAndModes.chatterCount, 350);
  assert.equal(merged.chatAndModes.messageCount, 1000);
  assert.deepEqual({ ...merged.chatAndModes.lastMessage }, { user: "alice", text: "pog" });

  assert.deepEqual([...merged.events], [...localState.transitions]);
  assert.equal(merged.raw.native, nativeSnapshot);
  assert.equal(merged.raw.local, localState);
});

test("mergeLogData gracefully handles null and empty inputs", async () => {
  const { mergeLogData } = await loadLogConsole();
  const merged = mergeLogData(null, null);

  assert.equal(merged.session.connectedSince, null);
  assert.equal(merged.session.nativeSupportsV2, false);
  assert.equal(merged.channel.name, "");
  assert.equal(merged.channel.live, false);
  assert.equal(merged.chatAndModes.submode, false);
  assert.equal(merged.chatAndModes.chatterCount, 0);
  assert.deepEqual([...merged.events], []);
});
