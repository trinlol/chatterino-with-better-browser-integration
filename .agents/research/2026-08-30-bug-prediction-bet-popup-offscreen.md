# Bug Report: Prediction Bet Popup Opens Off-Screen

**Date:** 2026-08-30
**Severity:** high
**Status:** fixed; offline verification complete, live Twitch observation pending

## Symptom

While a prediction was accepting entries, clicking the prediction (channel
points) element in the companion toolbar did not open a usable menu. The
prediction text still appeared in Chatterino, so from the viewer's side the
click only "printed the prediction to Chatterino" instead of letting them spend
Channel Points on an outcome.

## Expected Behavior

Clicking an outcome in the toolbar replica — or the channel points button while
a prediction is live — should surface Twitch's "How many Channel Points do you
want to use?" prompt beside the toolbar, fully on-screen and interactive, so the
viewer can pick an amount and submit. The engagement system message in
Chatterino is correct sync behaviour and must keep working.

## Reproduction Steps

1. Run the unpacked extension with Chatterino Better Browser attached, so
   companion mode is active and the native chat column is hidden.
2. Open a channel with a prediction that is accepting entries.
3. Click an outcome on the prediction element beside Follow.
4. Observe that no bet prompt appears; only the prediction system message shows
   up in Chatterino.

Offline equivalent (deterministic, no live prediction required):

```
npm run test:fixture:prediction-bet
```

## Root Cause Analysis

Two independent defects combined, both consequences of the replica pattern that
keeps Twitch's React-owned voting card mounted but parked off-screen.

### Locations

- **File:** `chatterino-extension/styles.css`
- **Rule:** `.chatterino-native-voting-source`
- **Cause:** the native voting card is parked at `left: -10000px` so only the
  toolbar replica is visible. Forwarding a replica click calls `.click()` on the
  parked native outcome control, and Twitch anchors its bet prompt to that
  control's rect. The prompt therefore opened at roughly `left: -9999px`,
  outside the viewport. Unlike the reward centre and chat identity popovers,
  nothing revealed or repositioned it.

- **File:** `chatterino-extension/voting-ui.js`
- **Function:** `isGenericVotingSurface`
- **Cause:** the bet prompt is a `role="dialog"` containing prediction-tagged
  buttons and the word "prediction", so it satisfied the generic voting-surface
  heuristic. Within one safety-poll cycle `findVotingSurface` scored the open
  prompt (40 + controls) above the real banner (20 + controls), adopted it as
  the banner source, parked it off-screen with `visibility: hidden`, cloned it
  into the toolbar, and republished its heading as the prediction title.

### When Introduced

Both followed from commit `b11b5eec33f68ebbcca5feb44f5b4356d9df39ef`
(2026-07-24), which introduced the replica pattern and the off-screen
`.chatterino-native-voting-source` parking after reparenting native nodes was
found to break Twitch's delegated click handlers.

## Implemented Fix

1. `voting-ui.js`: add `isPredictionBetPrompt` and reject bet prompts in
   `isGenericVotingSurface`, so an open prompt can never be adopted as the
   banner source. Exported for reuse and testing.
2. `content.js`: add `findNativePredictionDialog`,
   `positionNativePredictionDialog`, `ensurePredictionDialogScrollable`, and a
   100 ms watcher capped at 60 s. Placement reuses the reward dialog's
   measured-delta `translate()` technique because ancestors carry transforms and
   the prompt must never be reparented.
3. `content.js`: drive that positioning from both entry points — the prediction
   replica's forwarded activation, and `handlePointsReplicaClick` as a fallback
   when the points button answers with the bet prompt instead of the reward
   centre.
4. `styles.css`: add `.chatterino-native-prediction-dialog` reveal rules
   (visibility, pointer-events, opacity, z-index, scrollable children) and
   exclude it from the companion chat-hide rule.
5. Cleanup: stop the watcher and drop the marker class in `cleanupPredictionUi`
   and `resetChannelScopedUi`.

The engagement/system-message path was deliberately left untouched: the
"printed" prediction is correct sync behaviour, not part of the defect.

## Risks

- Twitch can rename its private DOM. The prompt is found by dedicated
  `prediction-bet` selectors first and by a points-phrasing text heuristic as a
  fallback, but future Twitch copy changes may need the pattern updated.
- The text heuristic is deliberately narrow (points phrasing only, not the word
  "vote") so a real banner is never mistaken for a prompt; a regression test
  pins both directions.

## Verification

- `scripts/prediction-bet-popup-fixture.mjs`: loads the real `protocol.js`,
  `activity-state.js`, `voting-ui.js`, `content.js`, and `styles.css` against a
  fixture Twitch DOM in headless Chromium. Before the fix the popup sat at
  `left: -9999`; after it, `left: 348, top: 125`, visible, with a bet chip click
  registering. Both entry points covered.
- Negative control: disabling only the `schedulePredictionDialogPositioning`
  call returns the popup to `left: -9999` and the harness fails, confirming the
  harness tests the fix rather than passing incidentally.
- `npm test`: 55 tests pass (53 previous plus 2 new), extension validation and
  release contract pass.
- `git diff --check` clean.

## Outstanding

- A live prediction was not available, so end-to-end confirmation on twitch.tv
  is still a manual observation. The fixture models the mechanism captured from
  the reporter's DevTools output (bet prompt in the ReactModal layer matching
  the points phrasing).

## Related

- `.agents/research/2026-07-24-bug-prediction-countdown-voting.md`
- `.agents/research/2026-07-24-bug-twitch-poll-voting-fullscreen.md`
