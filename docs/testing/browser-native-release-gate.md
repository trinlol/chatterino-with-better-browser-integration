# Browser/native release gate

This is the evidence contract for Chatterino Better Browser's native-overlay integration. A green Node harness proves deterministic native-messaging framing and fault fixtures. It does **not** prove the packaged desktop application, real Edge service-worker suspension, or Windows display geometry.

## Automated contract gate

Run this on every pull request that changes the extension, native-messaging code, the harness, or the release contract:

```powershell
npm run test:e2e:contract
npm run test:browser-native
```

The harness speaks Chrome native-messaging frames to a separate fake-host process. Its fixtures cover browser-first and desktop-first readiness, stale acknowledgements, host death, navigation, worker recreation, duplicate-channel windows, and a fail-open rejection inside two seconds. It records actual frames; it does not inspect source text to infer a pass.

First-attempt failures are release failures. A retry may collect logs, frames, and screenshots, but cannot change the result of the first attempt.

## Risk-based release matrix

| Gate | Cadence | Required environment | Required scenarios | Pass target |
| --- | --- | --- | --- | --- |
| Contract harness | Pull request | GitHub-hosted Windows runner | All fake-host lifecycle fixtures | Every first attempt passes. |
| Edge smoke | Pull request before merge | Dedicated Windows 11 machine, Edge Stable, 100% DPI, 100% zoom | Browser-first, desktop-first, two Twitch windows on the same channel | Independent sessions; no extension reload. |
| Packaged matrix | Scheduled and every pre-release | Dedicated Windows 11 machines with Edge Stable and packaged app/extension | DPI 100/125/150/200%; zoom 80/100/125%; normal, maximized, fullscreen, minimize/restore, and secondary display | 100 mixed cycles; fallback under 2 s; geometry within 2 physical pixels. |

Hosted GitHub Windows workers are contract-test evidence only. They are not claimed to be Windows 11 desktop, headed Edge, or physical display evidence. The Edge smoke and packaged matrix require a designated machine with the real package installed.

## Real-app smoke runner

The runner is intentionally opt-in, uses an isolated Edge profile, and changes neither registry nor user browser data:

```powershell
$env:CHATTERINO_E2E_ALLOW_PROCESS_CONTROL = "1"
$env:CHATTERINO_E2E_EDGE_PATH = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
$env:CHATTERINO_E2E_CHATTERINO_PATH = "C:\path\to\Chatterino Better Browser.exe"
powershell -ExecutionPolicy Bypass -File scripts\browser-integration\run-windows-smoke.ps1 -Launch
```

It deliberately exits non-zero after launching the dedicated test processes and prints the required observations. This prevents an unattended launch from being reported as proof of attachment, fallback, or geometry. The observer records the matrix results with the release evidence and stops only the printed process IDs when finished.

## Artifact collection after a failure

Collect the first-attempt native frame transcript, extension service-worker console, desktop log, the DPI/zoom/window state, and screenshots before retrying. Do not kill user processes, change native-messaging registration, or use a normal browser profile. Any process control requires `CHATTERINO_E2E_ALLOW_PROCESS_CONTROL=1` on the dedicated test machine.
