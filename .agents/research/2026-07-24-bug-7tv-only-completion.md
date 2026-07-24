# Bug Report: Emote completion includes non-7TV providers

**Date:** 2026-07-24
**Severity:** medium
**Status:** root-cause-found

## Symptom

The colon-triggered autocomplete popup and Tab completion include Unicode
emoji, Twitch, BetterTTV, and FrankerFaceZ results.

## Expected Behavior

Emote autocomplete and emote Tab completion should only offer channel and
global 7TV emotes.

## Reproduction Steps

1. Open a Twitch channel in Chatterino Better Browser.
2. Type an emote prefix such as `:cla`.
3. Observe emoji and non-7TV provider results in the popup or while cycling
   with Tab.

## Root Cause Analysis

### Location

- **File:** `src/controllers/completion/sources/EmoteSource.cpp`
- **Function:** `EmoteSource::initializeFromChannel`

### Cause

The shared emote source unconditionally collects every Twitch emote provider
and the Unicode emoji catalog. Both the popup and Tab completion build this
same unfiltered source.

### When Introduced

- **Commit:** `efa2e12bccaa7f36ee87ab4e186b1e00e7cc6d4e`
- **Date:** 2026-07-24
- **Author:** trinlol

## Proposed Fix

### Changes Required

1. Add an explicit 7TV-only provider mode to the shared emote source.
2. Use that mode for the popup and Tab completion production paths.
3. Add regression tests covering classic and smart completion strategies.

### Risks

- User and command completion must remain unchanged.
- The generic all-provider mode remains available for upstream-compatible
  tests and any non-fork callers.

### Tests Needed

- Popup-style classic and smart completion return only 7TV results.
- Tab-style classic and smart completion return only 7TV results.

## Related

- Upstream synchronization merge `413ec9c`.
- Failure count: 1 (`fix_failed_tests`): the initial Tab regression test
  incorrectly used the popup's colon-prefixed query instead of Tab
  completion's unprefixed query.
