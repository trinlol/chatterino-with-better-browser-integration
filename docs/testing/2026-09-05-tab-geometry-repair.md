# Edge overlay movement and tab-switch repair

Implemented after the user supplied a screenshot showing native chat outside the resized Edge window and reported that it stayed visible over another tab.

## Changes

- NativeMessaging now applies each valid select's geometry through AttachedWindow on the GUI thread, including already-ready sessions. The previous early acknowledgement skipped those updates and retained the previous horizontal offset.
- Tab selection uses the active tab rather than highlighted/multiselected tabs. Detachment does not require the browser window to own OS focus or the selected page's URL to be readable.
- Changing tabs invalidates outstanding measurement/acknowledgement work and cancels attachment retries. Current tab identity is rechecked after asynchronous browser calls and before sending a native select.
- An inactive Twitch tab cannot detach the visible tab's overlay.
- Returning to a detached session advances its generation and requests fresh geometry.
- Revealing Twitch chat no longer immediately requests another attachment/detach. Repeated detach is idempotent; measurements resume through existing resize/focus/recovery events.

## Verification

- Extension manifest, syntax, release contract and all 61 tests passed.
- Five new behavior tests cover unreadable non-Twitch tabs without OS focus, delayed resize after tab selection, inactive highlighted tabs, returning to Twitch, and the actual background/overlay feedback loop under fullscreen suppression.
- Native build targets chatterino-lib, chatterino-test and chatterino succeeded with the configured MSVC/Qt build environment.
- All 18 selected native protocol, session registry, IPC validation and manifest tests passed using Qt's offscreen plugin. The first headless launch lacked the plugin search path; it was stopped and rerun with the configured Qt 6.8 plugin directory.
- Changed files passed git diff --check.
- The rebuilt desktop executable was deployed to build/bin and restarted successfully as PID 6548. Binary timestamp: 2026-09-05 01:53:32 local time.
- Previous binary retained at C:/Users/danie/AppData/Local/Temp/chatterino-tab-geometry-backup-20260905/Chatterino Better Browser.exe.

## Remaining live check

Windows Computer Use stopped because it could not establish Edge's current URL confidently enough to enforce its policy. No further browser inputs were issued. Therefore the loaded unpacked extension still needs Reload in edge://extensions, followed by refreshing Twitch.

After reload, check browser movement, resize/maximize/restore, switching to the existing Extensions tab, and returning to Twitch. A visible native overlay with correct geometry and hiding/reappearing behavior remains the live acceptance criterion; automated test success is not that proof.

No unrelated source changes were reverted. No commit or release was created.
