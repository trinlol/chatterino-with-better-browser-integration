# Bug Report: Browser chat attach is lost across startup ordering

**Date:** 2026-07-31
**Severity:** high
**Status:** fixed and verified

## Symptom

Twitch replaces its native chat with the Chatterino placeholder, but the native
Chatterino chat window does not attach. Reloading the unpacked extension makes
the chat appear.

## Expected Behavior

The active Twitch chat attaches automatically regardless of whether Edge, the
native host, or the Chatterino desktop process starts first.

## Reproduction Steps

1. Start Edge and open a Twitch channel so the native-messaging host starts.
2. Leave Chatterino closed while the extension sends the initial chat geometry.
3. Start Chatterino and observe that the original geometry is not replayed.
4. Reload the extension and observe that the new geometry message attaches chat.

## Root Cause Analysis

### Location

- **File:** `src/util/IpcQueue.cpp`
- **Line:** 61
- **Function:** `ipc::sendMessage`
- **Related paths:** `src/BrowserExtension.cpp::runBrowserOutboundLoop`,
  `src/singletons/NativeMessaging.cpp::NativeMessagingServer::ReceiverThread::run`,
  and `chatterino-extension/background.js::connectPort`

### Cause

`ipc::sendMessage` opens the destination queue with `open_only`. If the desktop
`chatterino_gui` queue does not exist yet, the extension's first `select`
message is caught, logged, and permanently discarded. Neither side announced
when its inbound queue became available, so the extension had no event on
which to request fresh chat geometry. Reloading the extension worked only
because it produced a new `select` after the desktop queue existed.

### When Introduced

- **Commit:** `7255c65` (base IPC behavior), extended by `b6a2a3d` (browser
  outbound queue without a readiness replay)
- **Date:** 2026-06-04 / 2026-06-15
- **Authors:** Nerixyz / trinlol

## Implemented Fix

### Changes

1. The native host emits `native-host-ready` after creating its browser-bound
   queue.
2. The desktop receiver emits `desktop-ready` after creating
   `chatterino_gui`.
3. Either status makes the extension request fresh geometry from the active,
   focused Twitch tab.
4. A regression test covers both startup orders.

Together the two signals close both orderings: if the desktop starts first,
the later host-ready signal replays geometry; if the host starts first, the
later desktop-ready signal does so.

### Risks

- A readiness signal may request one extra chat measurement. Existing active
  tab and focused-window checks prevent attachment over an inactive browser.
- The existing one-way attach protocol still has no native-window creation
  acknowledgement; this fix addresses the confirmed startup loss specifically.

### Verification

- `node --test chatterino-extension/tests/background-attach.test.mjs`: 2 pass.
- `node scripts/validate-extension.mjs`: manifest, syntax, and 19 tests pass.
- Native Release target: `Chatterino Better Browser.exe` compiled and linked.
- Deployed executable SHA-256:
  `962DF9DF5A6A40EE25DD37A543195F920581F0DF9E28D8AAE0DCF3F3FE937F01`.
- The deployed 2.6.1 process launched successfully and the live Twitch overlay
  was visibly attached.

## Failure Count

0 hypothesis failures. One initial native build attempt had an external
toolchain-environment failure (missing MSVC include variables); rebuilding
through `VsDevCmd.bat` succeeded and does not count toward the three-failure
limit.

## Related

- Earlier lifecycle fix: active-tab `requestChatRect` replay.
- Prior diagnosis: extension-side `Chat: attached` is not native-window proof.
