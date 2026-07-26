# Bug Report: Attached chat trails a moving browser window

**Date:** 2026-07-25
**Severity:** medium
**Status:** fixed

## Symptom

The Chatterino chat overlay visibly trails behind its attached browser window
while the browser is dragged.

## Expected Behavior

The attached chat should stay visually locked to the Twitch chat region while
the browser is moving or resizing.

## Reproduction Steps

1. Enable the browser integration on a Twitch channel.
2. Drag the non-maximized browser window across the desktop.
3. Observe the native chat window following behind the browser.

## Root Cause Analysis

### Location

- **File:** `src/widgets/AttachedWindow.cpp`
- **Function:** `AttachedWindow::attachToHwnd`,
  `AttachedWindow::updateWindowRect`

### Cause

The native overlay polls the browser rectangle every 16 ms and calls
`MoveWindow(..., true)` unconditionally. This creates at least one polling
interval of movement latency and requests an immediate synchronous repaint on
every tick, even when the window has not moved. The extension's resize listener
also contains a no-op `queryChatRect;` expression, delaying its useful
measurement until a 475 ms timeout.

### When Introduced

- **Commit:** `c319767`
- **Date:** 2026-06-11
- **Author:** trinlol

Commit `c319767` changed the native follower interval from 1 ms to 16 ms to
reduce CPU usage, exposing the polling delay during browser movement.

## Implemented Fix

### Changes

1. Subscribe to `EVENT_OBJECT_LOCATIONCHANGE` and synchronize the overlay as
   soon as Windows reports movement of the attached browser.
2. Retain the 16 ms timer as a fallback and for foreground/Z-order maintenance.
3. Replace unconditional repainting `MoveWindow` calls with conditional
   `SetWindowPos` calls that preserve the existing surface during position-only
   changes.
4. Run the extension's geometry query immediately on resize while retaining the
   delayed final measurement for layout settling.

### Risks

- WinEvent callbacks must be unregistered with the overlay lifetime.
- The timer must remain available on systems where hook creation fails.
- Multi-monitor DPI transitions still need to flow through the existing scale
  calculation.

### Verification

- `AttachedWindow.cpp` compiled successfully with MSVC 19.50.
- The release executable linked successfully, was copied into `build/bin`, and
  the deployed SHA-256 matched the release output.
- All 17 extension JavaScript files passed `node --check`.
- All 18 extension tests passed.
- The release contract reports application/extension 2.6.0 and protocol v1.
- Prettier and `git diff --check` passed.
- Live browser dragging was confirmed fixed with the deployed build.

## Related

- `.agents/research/2026-07-24-bug-twitch-poll-voting-fullscreen.md`
- `chatterino-extension/tests/background-attach.test.mjs`

## Failure Count

0 countable hypothesis failures.
