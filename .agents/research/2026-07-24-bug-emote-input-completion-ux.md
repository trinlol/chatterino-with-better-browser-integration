# Bug: emote completion and input editor behave inconsistently

## Symptom

The emote autocomplete and message input feel inconsistent. Tab opens a visual
popup instead of cycling completions, and accepting or typing a 7TV emote can
turn the word into a rich image object. Further Tab presses and edits can then
replace the wrong range or feel unresponsive.

## Root cause

Two completion paths were active at once:

1. `SplitInput` intercepted an unmodified Tab and opened
   `InputCompletionPopup`.
2. `ResizingTextEdit` already implements Chatterino's inline Tab/Shift+Tab
   cycling with `QCompleter`.

Both completion paths converted accepted text into a `QTextImageFormat`. An
inline image occupies one `QTextDocument` cursor position, but the fork's
plain-text adapter exposes the full emote name. Completion and popup range
calculations mixed those two coordinate systems.

## Failure pattern

This is a state-model mismatch: visual objects and logical emote names have
different lengths, while the completion code assumes one shared coordinate
system. It was amplified by two competing handlers for the same Tab key.

## Fix

- Let the existing `ResizingTextEdit` Tab/Shift+Tab handler own Tab completion.
- Keep the visual popup for explicit `:` and `@` completion.
- Render Tab, popup, typed, and clicked emotes as inline images.
- When Tab cycles, replace the previous two-position `image + space` token
  instead of subtracting the expanded emote-name length.
- Derive popup selection ranges from the current document word rather than
  indexing expanded text with a document cursor position.
- Preserve the existing 7TV-only result filtering.

## Verification

- Added a focused regression test proving that a 7TV completion renders as an
  inline image, cycles to a second image, and preserves the following prefix.
- Native `InputCompletionTest.*`: 17/17 passed.
- Native `SplitInput/SplitInputTest.*`: 8/8 passed.
- Browser-extension validation: 18/18 passed.
- Windows application compiled, linked, hash-verified after deployment, and
  launched successfully.
- A final interactive smoke test of Tab, Shift+Tab, `:`, Space, Backspace, and
  cursor edits should be performed on a live channel with 7TV emotes loaded.

## Hypothesis failures

1. `design_rejected`: the first mitigation made all input plain text, which
   fixed cursor stability but removed the required inline-emote feature.
