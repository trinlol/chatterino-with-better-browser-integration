# Native helper disconnect after extension reload

The user supplied Edge's `Error when communicating with the native messaging host` error. The pasted background script contains the updated tab-revision checks, confirming the new extension code was loaded.

## Cause and reproduction

Windows Application events recorded native executable exits with `0xc0000409`, ucrtbase.dll, fast-fail parameter 5. An old helper (PID 1944) remained running and held the shared Desktop/native-host-stderr.log exclusively.

BrowserExtension.cpp reopened stderr with freopen_s against that shared file. On an open failure, freopen_s closes the original stream. Subsequent diagnostic output can invoke CRT invalid-parameter termination.

A real-executable probe, running with an isolated portable profile while the old helper still held the log, exited with decimal code 3221226505 (0xc0000409), emitted zero stdout bytes and never announced readiness. The same probe after the logging change returned a correctly framed native-host-ready message and exited normally on stdin EOF.

## Repair

- Use a per-process log at %TEMP%/chatterino-native-host-PID.log.
- Open the file independently; redirect the C++ diagnostic stream only after opening succeeds. Do not close/reopen the CRT stderr stream.
- Serialize diagnostic records from the native host's input/output threads with osyncstream.
- Protect each stdout length-prefix/payload pair with a mutex so the input thread's EOF response cannot interleave with an outgoing reply.
- Stop the stale helper processes and restart the desktop. Preserve the old chatterino_gui/chatterino_browser IPC files under %TEMP%/chatterino-ipc-before-host-recovery-20260905 before allowing their recreation.

## Validation

- MSVC native build succeeded.
- scripts/browser-integration/check-real-native-host.mjs runs the actual binary in six isolated portable profiles, two concurrent hosts at a time. All six send readiness and exit with code 0 after EOF; frame parsing succeeds. This covers the executable, not a fake host.
- All 61 extension validation tests passed.
- git diff --check passed for changed files.
- After recovery, the desktop and live Edge helper exchanged correlated chat-attached responses, visible in both sides' logs. The helper no longer died immediately after startup.
- Visual fullscreen placement remains unverified. Do not treat a native acknowledgement as proof of the final on-screen position.

Backups of prior native executables remain in the user's temporary directory. No registry or extension settings were changed and no user messages were sent.
