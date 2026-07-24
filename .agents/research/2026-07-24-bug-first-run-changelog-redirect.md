# Bug Report: First-run changelog opened upstream Chatterino

**Date:** 2026-07-24
**Severity:** medium
**Status:** fixed

## Symptom

The first-run version-change prompt offered to show a changelog, but accepting
it opened the original Chatterino changelog instead of the Chatterino Better
Browser release notes.

## Expected Behavior

The prompt should identify Chatterino Better Browser and open only this fork's
GitHub Releases page.

## Reproduction Steps

1. Keep the browser native-messaging manifest registered to the older
   `build/bin/Chatterino Better Browser.exe`.
2. Start the newly built release while the extension is reconnecting.
3. Observe that the extension starts the registered 2.5.5 executable first.
4. Accept its first-run changelog prompt and observe the upstream URL.

## Root Cause Analysis

### Location

- **File:** `src/Application.cpp`
- **Line:** first-run changelog block in `Application::initialize`
- **Runtime:** `%APPDATA%/Chatterino2/Misc/native-messaging-manifest-*.json`

### Cause

The current source already targeted the fork, but the browser integration was
still registered to an older 2.5.5 executable. That executable contained
`chatterino.com/changelog` and won the single-instance startup race, so the
user saw upstream behavior before the 2.6.0 build could become the main app.

### When Introduced

- **Commit:** runtime artifact predates `1167f745`
- **Date:** before 2026-07-24
- **Author:** local stale build artifact

Commit `1167f745` changed the source changelog destination to the fork, but the
native host continued to launch the pre-change executable until the registered
artifact was replaced.

## Implemented Fix

### Changes

1. Replaced the registered native-host executable with verified version 2.6.0.
2. Centralized the fork Releases URL as `LINK_CHATTERINO_RELEASES`.
3. Changed the prompt to explicitly say “Chatterino Better Browser release
   notes on GitHub”.
4. Extended release-contract validation so the prompt and URL cannot silently
   regress to upstream.

### Risks

- Low. The destination remains a normal HTTPS link opened through
  `QDesktopServices`.

### Tests

- Release-contract validation must require the fork Releases URL.
- Release-contract validation must require the fork-specific prompt.
- The active and registered executable must report version 2.6.0.

## Investigation Result

- **Hypothesis failures:** 0
- **Root cause:** confirmed by inspecting both binaries: the active 2.6.0
  binary contains the fork Releases URL, while the preserved 2.5.5 backup
  contains `chatterino.com/changelog`.

## Related

- Pull request #2
- `.agents/research/2026-07-24-bug-upstream-update-feed.md`
