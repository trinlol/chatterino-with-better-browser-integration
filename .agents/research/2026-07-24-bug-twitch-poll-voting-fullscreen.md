# Bug Report: Twitch polls lose voting controls and overlay fullscreen video

**Date:** 2026-07-24
**Severity:** medium
**Status:** fixed-and-verified

## Symptom

With the browser extension and Chatterino integration active, a Twitch poll can
produce an inaccurate or truncated announcement in Chatterino, while the
toolbar beside the follow button only shows a non-interactive title. The moved
toolbar controls remain above the video in fullscreen mode.

## Expected Behavior

- Chatterino should label a poll as a poll and use the exact structured title
  and choices where Twitch supplies them.
- The toolbar should show the native voting controls and allow a user click to
  reach Twitch's own handler.
- The entire extension toolbar should be hidden while video is fullscreen.

## Reproduction Steps

1. Load the unpacked extension and attach Chatterino to a Twitch channel.
2. Open a channel while a poll or prediction is accepting votes.
3. Observe the voting banner beside the follow button and try to activate a
   choice.
4. Enter Twitch player fullscreen and observe the fixed toolbar over the video.

## Root Cause Analysis

### Locations

- **File:** `chatterino-extension/content.js`
- **Lines:** 938, 1390, 1598
- **Functions:** `positionToolbarPortal`, `containsVotingBanner`,
  `ensureVotingReplica`
- **File:** `chatterino-extension/twitch-api.js`
- **Line:** 329
- **Function:** `getEventKind`
- **File:** `src/singletons/NativeMessaging.cpp`
- **Line:** 728
- **Function:** `NativeMessagingServer::composePredictionText`

### Cause

The extension reparented Twitch's React-owned poll and prediction nodes into a
fixed portal. Twitch's delegated click handlers depend on those nodes staying
inside the original React tree, so the visible controls could stop responding.
On the next sync, the code ignored the already-moved banner and could replace
it with a title-only fallback.

The generic GraphQL parser also treated any event containing `outcomes` as a
prediction, including typed community-poll events. The same voting card could
then be scraped through the pinned-message path, which flattened labels,
choices, and countdown fragments into inaccurate announcement text.

Finally, the portal used `display: inline-flex !important` and a near-maximum
z-index but had no fullscreen state gate. Normal inline `display: none` writes
could not override the important stylesheet rule.

### When Introduced

- **Commit:** `c3197678eb55b2b7ca3fc42f89004e40fee4419b`
- **Date:** 2026-06-11
- **Author:** trinlol
- **Change:** prediction movement and fixed toolbar portal
- **Commit:** `f49424330db73fba93481110f0de869e508684d3`
- **Date:** 2026-06-15
- **Author:** trinlol
- **Change:** poll movement copied the unsafe reparenting pattern

## Implemented Fix

### Changes

1. Keep native Twitch voting cards mounted off-screen in their original React
   tree, render a live toolbar replica, and forward each replica activation to
   the structurally matching native control.
2. Prefer structured GraphQL titles and choices, classify typed poll events
   before the generic `outcomes` fallback, and keep voting cards out of pinned
   message scraping.
3. Include a `kind` value in native messages so Chatterino renders `Poll`,
   `Poll closed`, and `Poll ended` instead of prediction wording.
4. Detect Fullscreen API state or a viewport-filling video, then hide the
   portal with an important inline display rule.

### Risks

- Twitch can change its voting-card DOM structure. The replica is rebuilt from
  the current source tree on every relevant mutation, while click forwarding
  uses the same element path in source and clone to limit selector coupling.
- Browser fullscreen without a Fullscreen API event uses a conservative
  viewport-coverage fallback and may require retuning if Twitch changes player
  geometry.

### Tests

- `chatterino-extension/tests/voting-ui.test.mjs`
- `chatterino-extension/tests/twitch-api-poll.test.mjs`
- Existing extension activation regression test
- Full native Release build of the `chatterino` target

## Failure Count

Zero countable hypothesis or fix failures. One initial native build invocation
used an uninitialized MSVC shell and could not find standard-library headers;
the same build passed after loading `vcvars64.bat`.

## Related

- `.agents/research/2026-07-24-bug-twitch-poll-voting-fullscreen.md`
