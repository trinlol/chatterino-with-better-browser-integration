# Bug Report: Native chat disconnects while Twitch window is inactive

**Date:** 2026-07-26
**Severity:** high
**Status:** implemented; live confirmation pending

## Symptom

The attached Twitch chat sometimes disconnects while its browser window is
inactive. Clicking inside the Twitch window makes the chat reconnect.

## Expected Behavior

The native messaging connection and attached chat should remain active while
the Twitch window is unfocused and idle.

## Reproduction Steps

1. Open a Twitch channel with the browser integration enabled.
2. Allow the native messaging host to connect.
3. Leave the Twitch window inactive without generating extension messages.
4. After approximately 10 seconds, observe that the native host exits.
5. Click the Twitch window and observe that a new geometry message reconnects
   the host and restores the chat.

## Root Cause Analysis

### Location

- **File:** `src/BrowserExtension.cpp`
- **Function:** `runLoop`

### Cause

The native messaging host started a watchdog that exited the process whenever
it received no browser message during a 10-second interval. An unfocused Twitch
window can legitimately produce no messages for longer than 10 seconds even
though its `connectNative()` port remains healthy.

The background extension clears its port when the host exits. It does not
reconnect until another content event needs the port, and clicking Twitch
causes `overlay.js` to publish a fresh chat rectangle. That click therefore
appears to repair the disconnected chat.

Chrome owns the lifetime of a connected native host: a `connectNative()` port
keeps the host running until the port is destroyed. The host already blocks on
stdin and receives EOF when Chrome closes that port, so a separate
message-activity timeout is both unnecessary and incorrect.

### When Introduced

- **Commit:** `7255c65`
- **Date:** 2026-06-04
- **Author:** Nerixyz

The idle watchdog was present when `BrowserExtension.cpp` was introduced.

## Implemented Fix

### Changes

1. Remove the 10-second message-idle watchdog and its state.
2. Keep the existing blocking native-message read loop.
3. Continue shutting down immediately when the browser closes the pipe and the
   host receives EOF.

### Risks

- A native host must still exit when Chrome destroys the port.
- The browser-to-native read loop must remain blocking rather than polling.
- The independent outbound IPC thread remains intentionally process-scoped.

### Verification

- Before the fix, an isolated deployed-host probe reproduced exit code 1 at
  approximately 10.1 seconds while stdin remained open.
- One initial probe against the raw release directory was invalid because that
  directory did not contain the deployed Qt runtime DLLs; this was an external
  test-environment issue, not a failed root-cause hypothesis.
- The MSVC 19.50 release build compiled and linked successfully.
- After the fix, the host remained alive for a 12.5-second idle interval,
  exceeding the old cutoff, then exited cleanly with code 0 immediately after
  stdin EOF.
- All 18 extension regression tests passed.
- Extension manifest, JavaScript syntax, release contract, clang-format, and
  `git diff --check` validation passed.
- The deployed executable SHA-256 matched the release output and the app
  launched successfully.
- Pending live user confirmation.

## Related

- `chatterino-extension/background.js`
- `chatterino-extension/overlay.js`
- `.agents/research/2026-07-26-bug-attached-window-maximize-lag.md`

## Failure Count

0 countable hypothesis failures.
