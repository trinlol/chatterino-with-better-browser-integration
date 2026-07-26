# Bug Report: Enter inserts a newline instead of sending chat

**Date:** 2026-07-26
**Severity:** medium
**Status:** fixed and release-ready

## Symptom
Pressing Enter in the Chatterino chat input inserted a new line instead of sending the message.

## Expected Behavior
Return and numpad Enter send the message. Ctrl+Enter and Ctrl+Shift+Enter send while retaining the input text, matching the configured defaults.

## Root Cause Analysis

### Location
- **File:** `src/widgets/splits/SplitInput.cpp`
- **Function:** `SplitInput::installTextEditEvents`

### Cause
Sending is normally wired as a `QShortcut` for the split input. When Qt delivered Return directly to `ResizingTextEdit::keyPressEvent` instead of activating that shortcut, the text edit's normal behavior inserted a newline. The saved hotkey remained correct; the input boundary lacked a fallback for missed shortcut dispatch.

## Implemented Fix
Handle Return and Enter in the text edit's key-event signal before `QTextEdit` can insert a newline. Preserve the default Ctrl and Shift combinations while allowing completion and spelling popups to consume Enter first.

## Verification
- Native MSVC build completed successfully.
- 25 focused `SplitInput` and `InputCompletion` tests passed.
- 18 extension and release-contract tests passed.
- Packaged 2.6.1 executable reported the correct product version.

## Failure Count
0 countable hypothesis failures.