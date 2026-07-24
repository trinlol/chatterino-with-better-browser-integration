# Bug Report: Prediction Countdown and Voting Controls

**Date:** 2026-07-24
**Severity:** medium
**Status:** fixed and verified

## Symptom

An active prediction produced a blue native banner whose timer remained near
`2:00 left`. The compact Prediction element beside Follow was visually present
but could not open Twitch's voting UI or submit a vote.

## Expected Behavior

The native banner should be green and count down from Twitch's real lock
deadline. If Twitch does not expose timing data, the banner should show no
invented timer. The toolbar element should open the native prediction surface,
copy the full interactive voting controls beside Follow, and forward clicks to
the corresponding Twitch controls.

## Reproduction Steps

1. Run the unpacked browser extension and Chatterino Better Browser.
2. Open a Twitch channel with an active prediction.
3. Observe the native banner's `2:00 left` countdown.
4. Click the compact Prediction element beside Follow.
5. Observe that no voting surface opens and no vote can be selected.

## Root Cause Analysis

### Locations

- **File:** `src/singletons/NativeMessaging.cpp`
- **Line:** 641
- **Function:** `NativeMessagingServer::updateEngagement`
- **Cause:** a missing duration was converted into a fabricated 120-second
  deadline on every new engagement state.

- **File:** `chatterino-extension/content.js`
- **Line:** 1957
- **Function:** `handlePredictionFallback`
- **Cause:** the fallback was a styled `div` with no activation behavior.

- **File:** `chatterino-extension/voting-ui.js`
- **Line:** 53 in the original implementation
- **Function:** `forwardActivation`
- **Cause:** copied controls were mapped to native controls only by exact child
  index paths, so Twitch wrapper changes could target a non-interactive node.

### When Introduced

- The fabricated 120-second fallback was introduced by commit
  `67ee0e00514cd5a1bac1c17cbadd203ad1151805` on 2026-07-24.
- The exact-path-only voting forwarding was introduced by commit
  `b11b5eec33f68ebbcca5feb44f5b4356d9df39ef` on 2026-07-24.
- The non-interactive fallback element was present in the integration code and
  retained through commit `cd87ed34d88c2f5a2d8207733090980aee8c43c9`.

## Implemented Fix

1. Normalize Twitch timestamps and anchor relative durations to one absolute
   deadline in the extension.
2. Send `closesAt` through the engagement protocol and preserve a prior valid
   deadline for repeated native updates.
3. Remove the native 120-second default; untimed predictions now render without
   a false countdown.
4. Render prediction highlights and the native prediction banner in green.
5. Replace the fallback `div` with an accessible green Vote button that opens
   Twitch's native prediction UI.
6. Discover the richest voting surface and copy it beside Follow.
7. Resolve cloned-control clicks by semantic label or interactive-control order
   when Twitch's wrapper structure differs.

## Risks

- Twitch can change its private DOM and GraphQL shapes. The implementation uses
  multiple semantic selectors and preserves a non-destructive fallback button,
  but future Twitch changes may require selector updates.
- A live prediction was not available during automated validation, so final
  Twitch end-to-end voting remains a manual observation.

## Verification

- Extension validation: 18 tests passed.
- Native focused validation: 6 tests passed.
- Windows Release targets `chatterino` and `chatterino-test` rebuilt.
- The rebuilt executable was installed with a recoverable pre-fix backup.
- `git diff --check` passed.

## Failure Count

- Countable failures: 1 `fix_failed_tests` (an initial C++ optional-presence
  check used pointer comparison and failed compilation; corrected to
  `has_value()`).
- Non-counting failures: build command timeouts and the missing Visual Studio
  developer environment on the first invocation.

## Related

- Existing integration PR:
  `https://github.com/trinlol/chatterino-with-better-browser-integration/pull/2`
