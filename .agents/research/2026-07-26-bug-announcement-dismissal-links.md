# Bug Report: Pinned announcement dismissal and links

**Date:** 2026-07-26
**Severity:** medium
**Status:** fixed, live-verified, and release-ready

## Symptom
Closing a pinned announcement hid it temporarily, but clicking elsewhere on Twitch reopened the same message. URLs in the announcement initially rendered as plain text.

## Expected Behavior
A dismissed pin should remain hidden until Twitch publishes a genuinely different pinned-message ID. HTTP and `www.` links should be clickable.

## Root Cause Analysis

### Location
- **File:** `src/widgets/splits/PinnedMessageWidget.cpp`
- **Functions:** `PinnedMessageWidget::setChannel`, `refresh`, `toggleUserPinned`

### Cause
The close action correctly stored the current Twitch pinned-message ID. Twitch repeatedly called `setChannel()` with the same widget, channel, and pin during normal page interaction. That method cleared the dismissed ID, and the following refresh showed the same pin again.

Live diagnostics reproduced the sequence twice: the close action stored the correct ID, then a same-channel rebind cleared it two to three seconds later.

## Implemented Fix
- Preserve the dismissed message ID across channel rebinding.
- Clear dismissal only when `refresh()` observes a different pin ID.
- HTML-escape announcement text and linkify only HTTP, HTTPS, and `www.` URLs.
- Open links externally through the label's browser interaction support.
- Remove temporary diagnostic logging from the release build.

## Verification
- User confirmed the dismissal remains effective during live Twitch interaction.
- Native MSVC build completed successfully.
- Packaged 2.6.1 runtime smoke test passed.

## Failure Count
2 countable `fix_failed_tests` attempts preceded runtime instrumentation. The instrumented trace identified the final root cause.