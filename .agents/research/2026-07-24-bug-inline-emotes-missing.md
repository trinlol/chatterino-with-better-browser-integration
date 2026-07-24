# Bug Report: Inline emotes disappeared from the input

**Date:** 2026-07-24
**Severity:** medium
**Status:** fixed

## Symptom

7TV autocomplete still returns names, but completed, typed, and clicked emotes
no longer render as images in the message input.

## Expected Behavior

The completion list must remain 7TV-only. Tab and Shift+Tab should cycle
reliably, while the selected emote is visibly rendered inside the input.

## Reproduction Steps

1. Type part of a 7TV emote name.
2. Press Tab or choose the emote from the visual completion popup.
3. Observe that the input contains only plain text.

## Root Cause Analysis

### Location

- **File:** `src/widgets/splits/SplitInput.cpp`
- **Lines:** 1390-1406 before this fix
- **Functions:** `insertCompletionText`, `insertEmote`

### Cause

The previous cursor-stability fix replaced all rich-emote insertion paths with
plain-text insertion. That avoided the old document-position mismatch, but also
removed the visible inline-emote feature instead of fixing image-aware
replacement.

### When Introduced

- **Commit:** `9a4b808`
- **Date:** 2026-07-24
- **Author:** trinlol

## Proposed Fix

### Changes Required

1. Restore rich image insertion for Tab, popup, typed, and clicked 7TV emotes.
2. Keep the completion model 7TV-only.
3. When Tab cycles, replace the previous image plus its trailing space using
   document positions rather than the expanded emote-name length.
4. Derive popup prefixes from the current word instead of mixing expanded text
   offsets with `QTextDocument` cursor offsets.
5. Add a regression test that cycles between two rendered inline emotes.

### Risks

- Rich image objects and logical text have different lengths. Every replacement
  path must remain document-position-aware.

### Tests Needed

- Tab completion renders a 7TV image.
- A second completion replaces the first image without corrupting the input.
- Existing 7TV-only source tests and SplitInput reply tests remain green.

## Related

- `.agents/research/2026-07-24-bug-emote-input-completion-ux.md`
- Failure count: 1 `design_rejected` (plain-text-only input removed a required
  feature).

## Verification

- Native `InputCompletionTest.*`: 17/17 passed, including inline rendering and
  image-to-image Tab cycling.
- Native `SplitInput/SplitInputTest.*`: 8/8 passed.
- Browser-extension validation: 18/18 passed.
- Windows application compiled and linked successfully.
