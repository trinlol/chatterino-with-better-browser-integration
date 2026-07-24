# Bug Report: Upstream Chatterino release offered as a downgrade

**Date:** 2026-07-24
**Severity:** medium
**Status:** fixed

## Symptom

Chatterino Better Browser 2.6.0 shows an update prompt for upstream Chatterino
2.5.5 and offers to download and install the older application.

## Expected Behavior

The application must check only releases from
`trinlol/chatterino-with-better-browser-integration`, and it must offer a
release only when its semantic version is newer than the running version.

## Reproduction Steps

1. Launch Chatterino Better Browser 2.6.0 with the updater enabled.
2. Let the automatic update check call the upstream Chatterino version service.
3. Open the update button and observe that upstream 2.5.5 is offered as an
   installable downgrade.

## Root Cause Analysis

### Location

- **File:** `src/singletons/Updates.cpp`
- **Line:** previous `checkForUpdates()` implementation, replaced at line 184
- **Function:** `Updates::checkForUpdates`

### Cause

The inherited updater queried `notitia.chatterino.com` and treated every
version that differed from the current build as `UpdateAvailable`. The
2.6.0 fork branding commit changed the local version but left that upstream
feed and its automatic-download links intact.

### When Introduced

- **Inherited updater commit:** `7255c6566a613e4b333189ef25ecc5a3c4d0a374`
- **Exposed by fork version commit:** `1167f745ff61483b15fe8457dbad4c7e3f6b6fe5`
- **Date exposed:** 2026-07-24

## Implemented Fix

1. Query only the fork's GitHub Releases API.
2. Ignore drafts and, unless beta updates are enabled, prereleases.
3. Select the highest valid semantic version and offer it only when it is
   newer than the running application.
4. Open the verified fork GitHub release page instead of downloading an
   upstream installer.
5. Add native version-comparison coverage and a release-contract guard that
   rejects upstream update endpoints.

### Risks

- Unauthenticated GitHub API requests are rate-limited. A failed request is
  reported as an update-check failure and never falls back to upstream.

### Tests

- Source-level release contract initially failed against the old endpoint.
- Extension and release-contract test suite passes after the fix.
- Native `Updates::isNewerThan` coverage includes newer, equal, older, and
  invalid versions.

## Hypothesis Results

- Root-cause hypothesis confirmed.
- One test-harness mismatch: the first endpoint assertion expected a single
  C++ string literal, while the URL is split across adjacent literals.
- Countable fix failures: 1.
