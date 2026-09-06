# Edge attachment investigation — 5 September 2026

## Conclusion

The failure is inside the native attachment lifecycle, beyond initial Edge extension registration. The captured native host log contains `attachment-rejected / invalid-browser-hwnd`, followed by repeated failure to deliver to the desktop IPC receiver. A current native fallback incorrectly assumes Edge directly parents its native host; the observed launch chain includes `cmd.exe`. This is a confirmed code defect matching the recorded rejection class, although the original rejected requests' incoming geometry/HWND messages were not captured alongside those responses.

The desktop GUI was absent at the beginning of this investigation. This independently explains the later receiver-unavailable failures. It does not establish why the desktop had been closed or prove that all historical failures have one cause.

No repair has been applied and successful live overlay attachment has not been established.

## Observed runtime evidence

- Edge Default profile loads unpacked extension `bogfpdfoagkaebimmlcbgmfmanhbhhlm` from this checkout's `chatterino-extension` directory.
- Edge's registered `com.chatterino.chatterino` manifest points to this checkout's `build/bin/Chatterino Better Browser.exe` and allows that extension ID.
- Executable timestamp: 2026-09-05 01:22:36 local time. Current source and old builds both identify as 2.6.2; version numbers alone do not establish matching artifacts.
- Initial sole Chatterino PID 28784 had `chrome-extension://bogfpdfoagkaebimmlcbgmfmanhbhhlm/ --parent-window=0` arguments. This selects native-host mode in `src/common/Args.cpp` and bypasses desktop initialization in `src/main.cpp`.
- Initial IPC directory contained `chatterino_browser`, with no `chatterino_gui` receiver file.
- Actual process ancestry: `msedge.exe (24424) -> cmd.exe (12516) -> native host (28784)`.
- Read-only Windows enumeration: parent CMD had hidden HWND 396050; grandparent Edge had visible HWND 263862.

The native host exclusively locks `C:/Users/danie/Desktop/native-host-stderr.log`. Its snapshot was captured after stopping only that host; Edge subsequently created replacement host PID 1944. Snapshot: `C:/Users/danie/AppData/Local/Temp/chatterino-attach-investigation-20260905/native-host-snapshot.log`.

Snapshot evidence:

```text
"status":"attachment-rejected", "reason":"invalid-browser-hwnd"
[BrowserExtension] failed to deliver message to chatterino_gui; status=1
```

- 36 lines contain `invalid-browser-hwnd`, representing 18 outbound frames logged twice (queue receive and browser send), including repeated request IDs. Do not interpret this as 36 independent failures.
- 254 log lines report delivery `status=1`, which is `QueueUnavailable` in `src/util/IpcQueue.hpp`; zero report queue-full `status=2`.
- Zero lines contain `status: chat-attached`.
- Later outbound select messages include the valid Edge HWND 263862 but fail IPC delivery. Therefore missing HWND does not explain those later failures; the absent desktop receiver does.
- The log also records desktop-ready announcements. Early rejection responses reference requests predating this host's start, so they are historical queued responses, not a complete paired live request trace.

## Concrete native defect

`src/BrowserExtension.cpp:133-223` assumes the helper's immediate parent is its browser. `browserWindowFromHostProcess()` enumerates only that parent's windows. In the observed Edge -> CMD -> host chain, it scans CMD, whose hidden window fails its visibility filter.

If startup/foreground capture provides no usable HWND, this fallback returns null. `src/singletons/NativeMessaging.cpp:1020-1033` rejects attachment with `invalid-browser-hwnd` rather than creating an overlay. The new session protocol made HWND validation mandatory; the checkpoint used a foreground fallback in the legacy path.

Repair target: resolve and validate the supported browser ancestor through the launcher chain, preserve validated per-window mappings, and handle multiple browser frames without arbitrarily attaching to the wrong one. Cover an unfocused Edge -> CMD -> native-host startup in a native test and real Edge observation.

## Other independently confirmed defects

1. **Unbounded detach feedback loop.** `overlay.js:167-175` sends detach while notifications/fullscreen suppress attachment; `background.js:1305-1313` responds with revealed state even for already undesired stored sessions; `overlay.js:251` immediately remeasures and sends detach again. A bounded VM reproduction using the actual background, lifecycle and overlay scripts produced eight detach/reveal cycles with another detach pending from one initial event. Make repeated detach idempotent and separate revealing from unconditional remeasurement. Persistent rejection can similarly initiate fresh attempts and defeat bounded retry handling. This was reproduced independently, not observed directly in the user's live Edge page.
2. **Geometry updates skipped.** `NativeMessaging.cpp:1063-1070` re-acknowledges an already-ready session and returns before applying new geometry. `background.js:335-354` preserves generation when the channel is unchanged. This can leave an attached overlay at its previous size; it is not the explanation for initial nonattachment.

## Validation and limits

- Three parallel agents reviewed extension lifecycle, native attachment, and regression history independently.
- `npm run test:extension` passed 56/56, including manifest/syntax/release validation.
- Current source already includes the request-ID and legacy-lease corrections from prior work; those were not rediscovered as unfixed defects.
- The named native E2E harness exchanges fixture messages with a fake host. It does not run the actual Edge extension with the desktop binary, and cannot prove native overlay attachment.
- Most relevant feature work is uncommitted after HEAD `78efab9e`; commit bisection cannot attribute all changes to a specific feature or author.
- Existing BUILD-ISSUE.md is stale relative to the September 5 binary; previous FIXES-APPLIED.md claims are not substitutes for recorded runtime evidence.
- A normal desktop launch created PID 27308. At the subsequent window inventory it had a hidden main Qt window and helpers, with no Edge-owned native overlay. The desktop-ready announcement reached the native host.
- Diagnostic `--verbose` launches exited with `0xc0000409` in ucrtbase.dll. Normal launch remained running. This is a diagnostic-path issue, not evidence that normal desktop startup crashes; `AttachToConsole.cpp` passes a null output argument to `freopen_s` and warrants separate correction.
- Browser automation exposed no existing Edge surface, so no actual Twitch-page screenshot or foreground/tab-switch verification was possible through that interface.

## Recommended repair sequence

1. Correct native browser-ancestor resolution and validate desktop receiver availability with explicit error reporting.
2. Stop detach/reveal and rejection feedback loops; add tests connecting the real background and overlay scripts.
3. Apply geometry updates for existing ready sessions.
4. Build and deploy a matched native/extension pair, then verify real Edge startup in both orders, unfocused startup, notifications/fullscreen recovery, tab return, and resizing using actual native overlay ownership and visible geometry.

Only this investigation report was added to the checkout. Existing source changes were preserved. Runtime diagnostics started the desktop and briefly restarted its Edge helper; no registry settings, extension preferences, or user messages were changed by the investigation.
