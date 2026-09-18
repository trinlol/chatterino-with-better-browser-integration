# Marquee Event Bar Polish and History - Plan

## Goal Capsule

- **Objective**: Deliver high-polish visual alignment and compact formatting to the top-bar marquee ticker and recent events history popup, while establishing a Twitch Helix backfill mechanism for broadcaster accounts.
- **Product Authority**: Owns the Marquee widget rendering, event formatting, ticker speed, history dialog layout symmetry, and Helix subscription backfill. Surrounding topics (Twitch Native Chat GIFs and Kick Chat integration) are deferred as contextual candidates and are not active scope for this artifact.
- **Open Blockers**: None. Requirements are settled and ready for implementation.

<!-- ce-section: work-relationships -->
### How This Work Fits Together

This plan owns the immediate Marquee event bar visual polish and historical event backfill. Broader multi-platform and media integration work is staged as follows:

- **Marquee Event Bar Polish and History (This Plan)**: Streamlined `{username} (x mo)` ticker formatting, baseline font cap-height badge alignment, faster scrolling, symmetrical history cards, and broadcaster Helix backfill.
- **Twitch Native Chat GIFs (Subsequent Plan)**:
  - Can proceed independently of this plan.
  - Adds chat message parsing for Twitch inline GIPHY links (`[Gif of ...]`), image fetching, and larger-than-standard inline animated emote rendering.
- **Kick Chat Integration & Combined Mode (Final Phase Staging)**:
  - Depends on stable Chatterino split architecture.
  - Introduces Kick Pusher/WebSocket protocol, green tab indicators, global Kick emotes, moderation tools, combined chat streams with platform origin badges, and per-user message repeat deduplication.

## Product Contract

### Summary

Refine the 1-line marquee ticker into an ultra-compact live feed displaying `{username} (x mo)` with subscriber and cheer badges sized to match text capital height and aligned to the font baseline, increase default scroll speed to 45 px/s, restore symmetrical rounding to the events history popup cards without scrollbar clipping, and introduce an automatic Helix subscription backfill on split load for broadcaster and moderator accounts.

### Problem Frame

The current marquee bar shows verbose phrasing such as `{username} Subscribed (Tier 1) (Tier 1)` or `{username} Resubscribed for 6 months (6 mo)`, consuming substantial horizontal width on a single-line ticker. In addition, sub badges are rendered with fixed vertical offsets and arbitrary centering that do not match the capital height of the UI font. In the Recent Events History popup, the card containers sit flush against the right viewport edge, causing their right-hand rounded corners to appear clipped. Finally, when opening a channel while events occurred offline, viewers and broadcasters currently see an empty ticker until new real-time IRC notices arrive.

### Key Decisions

- **Compact Parenthesis Formatting** (session-settled: user-directed — chosen over verbose sentences: eliminates ticker bloat and maximizes visible events). Governs R1, R2.
- **Font Cap-Height Badge Sizing and Baseline Alignment** (session-settled: user-directed — chosen over generic 16px boxes: ensures badges match the visual height of capital letters and rest on the text baseline). Governs R3.
- **Scroll Speed Default Increase** (session-settled: user-approved — chosen over 35 px/s: creates a more responsive and modern ticker pace). Governs R4.
- **Symmetrical History Card Viewport Margins** (session-settled: user-directed — chosen over edge-flush cards: prevents right-side border-radius clipping by the scroll area). Governs R5.
- **Helix API Backfill Restricted to Broadcaster/Moderator Accounts** (session-settled: user-approved — chosen over public polling: honors Twitch OAuth scope restrictions while delivering offline sub history for channel managers). Governs R6, R7.

### Requirements

#### Ticker Display & Formatting

- R1. Subscription and resubscription events on the marquee bar must display in the compact format `{username} ({months} mo)` (e.g., `ValkyrieQueen (6 mo)` or `apex_legend (1 mo)`).
- R2. Non-subscription events must follow the same compact parenthesis style:
  - Cheer events: `{username} ({bits} bits)` (e.g., `NightRider (500 bits)`).
  - StreamElements tip events: `{username} ({amount})` (e.g., `CryptoDonor ($25.00)`).
  - Gift subscription events: `{username} ({count} gifts)` (e.g., `PajladaFan (5 gifts)`).
- R3. Subscriber badges, cheer badges, and tip badges rendered on the ticker must match the capital letter height (`metrics.capHeight()`) of the active UI font and align top-to-bottom with the font baseline.
- R4. The default scrolling speed setting (`marqueeSpeed`) must be increased from 35 px/s to 45 px/s.

#### Events History Dialog Symmetry

- R5. Event cards in the Recent Events History dialog (`MarqueeHistoryDialog`) must have symmetrical left and right margins within the scroll area viewport, ensuring that all four rounded corners (`border-radius: 8px`) are fully visible without being truncated by the vertical scrollbar.

#### Historical Event Backfill

- R6. When opening or switching to a Twitch channel where the current user is authenticated as the broadcaster or channel moderator, Chatterino must query Twitch Helix (`GET /helix/subscriptions`) to populate up to the 15 most recent subscribers.
- R7. For viewer channels without broadcaster/moderator permissions, Chatterino must continue streaming live events via IRC `USERNOTICE` without making failing unauthorized Helix requests.

### Key Flows

#### 1. Compact Ticker Stream
- An incoming sub, resub, cheer, or tip event is received.
- The event item is parsed with compact text `{displayName} ({amount/months})`.
- The ticker computes the badge rect matching `metrics.capHeight()` and draws the badge at `y = baseline - capHeight`.
- The text is drawn directly on `baseline`.
- The item scrolls across the bar at 45 px/s (pausing on mouse hover).

#### 2. Broadcaster Channel Load Backfill
- User navigates to a channel split where they hold broadcaster or mod privileges.
- If the marquee event buffer for the channel has fewer than 5 events, a background Helix request fetches recent subscriptions.
- Retrieved subscriptions are converted to `MarqueeEvent` objects and prepended to the channel's `recentMarqueeEvents()`.
- The marquee bar smoothly slides down if previously collapsed and begins displaying the populated ticker.

### Acceptance Examples

- **Example 1 (Resub Ticker Item)**: User `shroud` receives a 12-month resub from `Justin`. The marquee displays `[Sub Badge] Justin (12 mo)` with the badge exactly matching the capital letter height of `Justin`.
- **Example 2 (History Dialog Symmetry)**: Opening the Recent Events History popup displays cards with uniform 8px padding on the left and right sides of the viewport, with crisp 8px rounded corners visible on both sides.
- **Example 3 (Viewer Channel)**: An ordinary viewer joins `#esl_csgo`. No unauthorized `401/403` Helix subscription requests are dispatched; the marquee displays "No recent events" or stays collapsed until a live IRC event arrives.

### Scope Boundaries

- **In Scope**:
  - Compact text generation in `src/providers/twitch/IrcMessageHandler.cpp` and `src/widgets/splits/MarqueeWidget.cpp`.
  - Cap-height badge scaling and baseline rendering in `src/widgets/splits/MarqueeWidget.cpp`.
  - Increasing default `marqueeSpeed` in `src/singletons/Settings.hpp`.
  - Viewport margin and scroll padding adjustments in `src/widgets/dialogs/MarqueeHistoryDialog.cpp`.
  - Broadcaster Helix subscription backfill integration in `src/providers/twitch/TwitchChannel.cpp`.
- **Out of Scope**:
  - Inline rendering of Twitch native chat GIFs (deferred to separate plan).
  - Kick chat protocol, tabs, emotes, combined chat mode, and moderation tools (deferred to separate plan).
  - Third-party donation platform backfills (e.g., Streamlabs).

## Planning Contract

### Summary

Implement the refined marquee visual styling and historical backfill across 4 discrete units: (1) compact text and speed tuning, (2) exact cap-height badge scaling and baseline alignment, (3) symmetrical card layout for the history popup, and (4) Helix subscription backfill for broadcaster channels.

### Key Technical Decisions

- **KTD1 (Compact Sub String Generation)**: Modify `IrcMessageHandler.cpp` so that `event.amountText` stores `%1 mo` (or `1 mo` for tier 1 new subs) and `event.detailText` is empty or concise, so the ticker renders purely `{displayName} ({amountText})`. Governs R1, R2.
- **KTD2 (Baseline-Locked Painter Coordinates)**: Instead of using `Qt::AlignVCenter` bounding rectangles that calculate vertical position with font descent/leading, compute the exact `textBaseline = (height - metrics.height()) / 2 + metrics.ascent()`. The badge is drawn at `(x, textBaseline - capH, capH, capH)` and the text baseline is drawn exactly at `textBaseline`. Governs R3.
- **KTD3 (Viewport Symmetrical Insets)**: Configure `listLayout_->setContentsMargins(4, 4, 8, 4)` and `scrollArea_->viewport()->layout()` spacing so card borders have 8px margin on both sides regardless of whether the vertical scrollbar is displayed. Governs R5.
- **KTD4 (Helix Subscription Endpoint)**: Add `getSubscriptions(QString broadcasterId, ResultCallback<std::vector<HelixSubscription>> successCallback, HelixFailureCallback failureCallback)` to `src/providers/twitch/api/Helix.hpp` and `Helix.cpp` hitting `GET subscriptions?broadcaster_id={id}&first=15`. Governs R6, R7.

## Implementation Units

### Unit 1: Marquee Event Text Formatting & Speed Configuration
- **Files**:
  - `src/providers/twitch/IrcMessageHandler.cpp`
  - `src/widgets/splits/MarqueeWidget.cpp`
  - `src/singletons/Settings.hpp`
- **Tasks**:
  - In `IrcMessageHandler.cpp`, format new subscriptions as `amountText = "1 mo"` and resubscriptions as `amountText = QString("%1 mo").arg(monthsStr)`. Set `detailText` to empty for standard subs/resubs.
  - In `MarqueeWidget.cpp`, adjust item width and rendering logic to cleanly render `{displayName}` followed by `({amountText})`.
  - In `Settings.hpp`, update the default `marqueeSpeed` setting value from 35 to 45.
- **Verification**: Verify that simulated and live subs render as `{username} (x mo)` and ticker scrolls at 45 px/s.

### Unit 2: Exact Font Cap-Height Badge Scaling & Baseline Alignment
- **Files**:
  - `src/widgets/splits/MarqueeWidget.cpp`
- **Tasks**:
  - Calculate `const int capH = metrics.capHeight() > 0 ? metrics.capHeight() : metrics.ascent()`.
  - Set badge dimensions to `capH` x `capH`.
  - Compute `const int textBaseline = (this->height() - metrics.height()) / 2 + metrics.ascent()`.
  - Render badges at `y = textBaseline - capH`.
  - Render text using `painter.drawText(drawX, textBaseline, text)` so that all characters and badges share an identical baseline.
- **Verification**: Inspect marquee ticker and confirm that badges match capital letter height and sit flush on the baseline.

### Unit 3: Recent Events History Dialog Symmetry & Viewport Margins
- **Files**:
  - `src/widgets/dialogs/MarqueeHistoryDialog.cpp`
- **Tasks**:
  - Adjust `listLayout_` contents margins to `(2, 4, 8, 4)` or adjust scroll area viewport margins to provide symmetrical horizontal spacing.
  - Ensure event card containers retain their `8px` rounded borders cleanly on both left and right edges.
- **Verification**: Open Recent Events History dialog and verify that cards have equal margins on both left and right, with rounded right corners visible.

### Unit 4: Twitch Helix Broadcaster/Mod Subscription Backfill
- **Files**:
  - `src/providers/twitch/api/Helix.hpp`
  - `src/providers/twitch/api/Helix.cpp`
  - `src/providers/twitch/TwitchChannel.hpp`
  - `src/providers/twitch/TwitchChannel.cpp`
- **Tasks**:
  - Add `HelixSubscription` struct and `getSubscriptions(...)` to `Helix.hpp` and `Helix.cpp`.
  - In `TwitchChannel.cpp`, when a channel is joined or refreshed, check if the current user has broadcaster or moderator authorization.
  - If authorized and `recentMarqueeEvents_.empty()`, dispatch `getHelix()->getSubscriptions(...)` for the top 15 subscriptions, convert them into `MarqueeEvent` structs, and load them into `recentMarqueeEvents_`.
- **Verification**: Joining a broadcaster split automatically populates recent subscriptions into the marquee bar and history popup.

## Verification Contract

### Automated Tests
- Build `chatterino` and `chatterino-test`:
  ```bash
  cmake --build build --target chatterino --config Debug
  cmake --build build --target chatterino-test --config Debug
  ```
- Run CTest Marquee suite:
  ```bash
  ctest --test-dir build --output-on-failure -R Marquee
  ```

### Manual Verification
1. Start Chatterino, open a Twitch channel split.
2. Trigger mock subs/resubs, confirm display format: `User (1 mo)`, `User (6 mo)`.
3. Verify badges scale to text capital height and align on the baseline.
4. Open the Recent Events History popup, verify symmetrical card margins and rounded corners on both sides.

## Definition of Done

- Marquee ticker displays `{username} (x mo)` format.
- Sub badges match text cap-height and baseline.
- Default ticker speed is 45 px/s.
- History dialog cards have symmetrical rounded corners without scrollbar cutoff.
- Helix backfill queries recent subs for broadcaster/moderator channels.
- All unit tests pass with zero regressions.
