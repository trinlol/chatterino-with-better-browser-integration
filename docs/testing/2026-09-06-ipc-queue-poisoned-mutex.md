# IPC queue poisoned-mutex hang (2026-09-06)

## Symptom

After an unclean shutdown (force-killed browser or GUI), the browser
extension's attachment flow died silently: the native host received the
first `sync` from the service worker, forwarded it into the
`chatterino_gui` IPC queue, and then never processed another message.
The desktop never replied `desktop-ready`, no `select` was ever
answered with `chat-attached`, and the extension reported a healthy
connection while chat never attached.

Host logs showed exactly one stdin message processed, no EOF, and no
`failed to deliver` line — the loop simply stopped between the first
`sendMessage` and the next `receiveFromBrowser`.

## Root cause

Windows boost::interprocess message queues emulate their interprocess
mutex by spinning on shared memory (`BOOST_INTERPROCESS_WINDOWS`
without kernel mutexes for the queue header). A process that dies
while holding that mutex leaves it locked forever; the kernel cannot
recover an emulated mutex because it is just bytes in the memory-mapped
file.

In this incident a GUI process was force-killed mid-operation on
2026-09-05 ~02:03. From that point on:

- every host's `try_send` into `chatterino_gui` spun at 100% CPU
  inside `ipc::sendMessage` — orphaned hosts had burned 20–260 s of
  CPU each doing nothing;
- the relaunched GUI's `ReceiverThread::receive()` spun the same way
  (905 s CPU observed);
- `chatterino_gui`'s queue file last-write time froze at the moment
  of the kill while every other IPC file kept updating — the
  on-disk tell-tale for this failure.

`try_send` bounds only the queue-full check; acquiring the internal
mutex is an unbounded spin. That is why the "non-blocking" send still
hung.

## Reproduction (deterministic, no browser needed)

1. Force-kill any process holding the queue mutex mid-operation
   (or reuse a queue file from such a crash).
2. Start the host binary manually with
   `--parent-window=0 "chrome-extension://<id>/"` and a redirected
   stdin pipe.
3. Write three length-prefixed JSON frames.

Result before the fix: exactly 1 of 3 messages processed, host CPU
pegged, no delivery-failure log. Result after: 3 of 3 processed.

## Fix

Two changes (commit `fix(ipc): recover from force-killed peers
poisoning queue mutexes`):

1. **Bounded operations** (`src/util/IpcQueue.cpp`): `sendMessage`
   uses `timed_send` and `receive()` waits in 250 ms
   `timed_receive` slices, so a poisoned queue surfaces as
   `QueueFull`/`QueueUnavailable` or keeps the receiver responsive
   to interruption instead of hanging the process forever.
2. **Replace on GUI start** (`src/singletons/NativeMessaging.cpp`):
   the `ReceiverThread` opens `chatterino_gui` with
   `tryReplaceOrCreate` — the same recovery the native host already
   applies to `chatterino_browser` — so each GUI launch discards any
   stale or poisoned queue file instead of inheriting it.

## Verification

- Manual host driver: 3/3 frames processed post-fix (1/3 before).
- Twin Edge profile + Twitch tab + fresh GUI: full round trip
  observed in host logs (`sync` → `select` → `chat-attached` ACK →
  `detach` → `detached`, ~10 messages, `browserHwnd` resolved).
- GUI force-killed mid-flight, relaunched, browser relaunched:
  attachment completes again.
- `chatterino-test --gtest_filter='IpcQueue*:NativeMessaging*'`:
  16/16 pass, including the new
  `SendToAbsentQueueFailsFastInsteadOfHanging` and
  `ReplacedQueueAcceptsNewMessages` regression tests.

## Operator notes

- The on-disk signature of a poisoned queue is an IPC file whose
  last-write time predates the processes that should be using it
  (`%APPDATA%\Chatterino2\IPC\chatterino_gui`).
- With the fix, deleting the stale file is no longer required — a
  GUI relaunch replaces it. But if a *running* GUI is suspected
  hung, its queue file timestamp freezing is the diagnostic.
- Edge writes `prefs.tracked_preferences_reset` audit entries when
  tracked preferences change externally; seeing a spliced-out
  extension id there is expected and harmless.
