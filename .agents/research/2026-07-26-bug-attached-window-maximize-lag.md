# Bug Report: Attached chat trails maximize and restore transitions

**Date:** 2026-07-26
**Severity:** medium
**Status:** implemented; live confirmation pending

## Symptom

The attached Chatterino chat visibly trails the browser when the title bar is
double-clicked to switch between maximized and windowed states.

## Expected Behavior

The overlay should remain visually attached to the browser without replaying
the intermediate positions from the maximize or restore animation.

## Reproduction Steps

1. Enable the browser integration on a Twitch channel.
2. Double-click the browser title bar to maximize it.
3. Double-click the title bar again to restore it to a window.
4. Observe the attached chat moving through delayed intermediate positions.

## Root Cause Analysis

### Location

- **File:** `src/widgets/AttachedWindow.cpp`
- **Functions:** `handleLocationChange`,
  `AttachedWindow::syncToTargetWindow`

### Cause

The drag-lag fix subscribes to top-level
`EVENT_OBJECT_LOCATIONCHANGE` notifications and applies every reported browser
rectangle immediately. Out-of-context WinEvent notifications from another
process are queued and delivered sequentially. A maximize or restore animation
therefore causes the overlay to replay its burst of intermediate rectangles
instead of moving directly to the final settled rectangle.

### When Introduced

- **Commit:** `1d58204`
- **Date:** 2026-07-25
- **Author:** trinlol

That commit correctly removed the polling lag from ordinary browser dragging,
but did not distinguish continuous direct manipulation from an animated window
state transition.

## Implemented Fix

### Changes

1. Observe top-level window state changes as well as location changes.
2. Detect transitions between maximized and restored states with `IsZoomed`.
3. Temporarily hide the overlay during that animation and debounce the
   cross-process event burst for 75 ms.
4. Apply only the final settled browser rectangle, then restore the overlay if
   the integration still requests it to be visible.
5. Keep ordinary drag synchronization immediate and retain the 16 ms fallback
   outside state transitions.
6. Defer final placement if a transition is interrupted by minimizing the
   browser.

### Risks

- Visibility requested by the extension must not be overridden when a
  transition finishes.
- Minimizing during a transition must not make the overlay reappear over other
  applications.
- Browser F11 fullscreen remains controlled by the existing extension
  `fullscreen` state and must not be mistaken for a title-bar zoom transition.

### Verification

- The MSVC 19.50 release build compiled and linked successfully.
- All 18 extension regression tests passed.
- Extension manifest and JavaScript syntax validation passed.
- The release contract reports application/extension 2.6.0 and protocol v1.
- Clang-format and `git diff --check` passed.
- The deployed executable SHA-256 matched the release output and the app
  launched successfully.
- Pending live maximize/restore confirmation.

## Related

- `.agents/research/2026-07-25-bug-attached-window-drag-lag.md`
- `src/widgets/AttachedWindow.hpp`

## Failure Count

0 countable hypothesis failures.
