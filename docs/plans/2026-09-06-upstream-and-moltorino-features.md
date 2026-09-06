# Upstream & Moltorino feature audit (2026-09-06)

## Upstream chatterino2 — what we're missing

Our merge base with `Chatterino/chatterino2` master is `58f9ff32`
(fix #7176). Upstream is **39 commits ahead**. User-facing items:

### Features
- **Twitch GIFs as links** (#7222) — animated Twitch emotes link to their GIF.
- **ASCII art wrapping** (#7199, #7235) — ASCII art wraps at default web-chat
  width; reduced wrapping width for better rendering.
- **Strong view aliases** (#7194) — plugin-facing; aliases that keep working
  across recreation.
- **Single-message specials** (#7196 announcements, #7203 subscriptions,
  #7205 watchstreaks) — Twitch "special" messages (sub announcements, streaks)
  collapse into one message instead of stacked system lines.
- **Local-time account/follow age** (#7237).
- **Incremental history search returns plain text** (#7227).

### Fixes worth having
- Entire-width message separation line (#7215)
- Non-text element selection easier (#7232)
- Input focus lost on another tab (#7230)
- Partially overlapped emotes in ignore list (#7224)
- Optional image-uploader settings cleared on import (#7225)
- `length` stored for Twitch specials (#7220)
- macOS incognito links (#7183)

### Refactors that raise merge cost the longer we wait
- **Message tokenization refactor** (#7221) — rewrites message parsing paths.
- **Twitch emote parsing generalization** (#7219) + allocate-less parsing (#7187)
- TwitchIrcServer cleanup (#7178), string-view emote lookup (#7200)
- QtSvgWidgets dependency removed (#7139) — affects SvgButton users (Moltorino
  port depends on SvgButton; fine, it stays available).

**Recommendation: sync upstream BEFORE porting Moltorino features.** The
tokenization refactor touches the same parsing code the prediction feature
hooks into (channel messages carry prediction state); porting onto the old
base would double our conflict surface.

## Moltorino (codeberg.org/MoltoBenne/Moltorino) — feature menu

MIT-licensed fork of Chatterino7 (itself a fork of chatterino2). Their fork
adds on top of chatterino7:

| Feature | Files | Size | Our interest |
| --- | --- | --- | --- |
| **Prediction popout** (create/manage/bet/resolve, live banner with animated outcome bars + countdown) | `PredictionDialog` (3.7k lines), `PredictionBanner` (963), `TwitchChannel::PredictionEvent`, `TwitchGql` API | Large | **User favorite — port** |
| Poll popout + banner | `PollDialog`, `PollBanner` | Medium | Natural follow-on, same plumbing |
| Pinned messages banner | `PinnedMessageBanner` (931) | Medium | We already have PinnedMessageWidget — compare |
| Channel points dialog | `ChannelPointsDialog` | Medium | Optional |
| `/nuke` mass-moderation command | `commands/builtin/twitch/Nuke` | Small | Nice mod tool, cheap to port |
| Blocked terms / founders / mod-vip commands | `BlockedTerms`, `GetFounders`, `ModVipActions` | Small | Cheap wins |
| Kick support | entire `providers/kick` tree | Very large | Out of scope for now |
| Homies badges, Moltorino presence/auth/telemetry settings | `providers/homies`, `providers/moltorino` | Medium | **Skip** (their service, not ours) |
| Tray controller | `controllers/tray` | Small | Optional, evaluate |

### How their prediction feature works (port notes)
- `TwitchChannel` owns `PredictionEvent` (outcomes 2–10, window seconds,
  refresh/in-flight tracking, `predictionChanged` signal, UniqueAccess guard).
- EventSub/PubSub update → `handlePredictionUpdate` → banner + any open dialog
  re-render in place (`updateInPlace`, layout generation counter).
- **Broadcaster view**: create (title, outcomes, duration), templates picker,
  resolve/lock/cancel via **Twitch GQL** (`TwitchGql` — private API, needs
  their scope set: `channel:manage:predictions` etc.).
- **Viewer view**: bet with channel points (amount selector, outcome list).
- Banner: metadata line, countdown timer, animated per-outcome fraction bars
  (QVariantAnimation), dismiss-per-persistent-key, toggle button.
- Auth: Moltorino re-auths via an **Android TV client ID** to get scopes the
  desktop client ID doesn't request. Porting needs a decision: reuse their
  client-id approach or instruct users to use an account with the scopes.

### Crediting (per user request)
- Add `docs/CREDITS.md` (or extend README credits section): Moltorino
  (https://codeberg.org/MoltoBenne/Moltorino), MIT, and Chatterino7
  (https://github.com/SevenTV/chatterino7) as the upstream of the ported code.
- Per-file header comment on ported files: "Ported from Moltorino (MIT),
  Copyright (c) MoltoBenne" where the file is a direct derivative.

## Proposed execution order

1. **Sync upstream** (merge 39 commits; expect conflicts in parsing paths we
   touched for Better Browser — `IrcMessageHandler` especially).
2. **Port `/nuke`, blocked-terms, founders, mod-vip commands** (small,
   independent, immediate value).
3. **Port prediction popout** (PredictionEvent plumbing → banner → dialog →
   GQL + auth decision). Add tests for PredictionEvent parsing/states.
4. Evaluate poll popout + pinned banner vs our existing pinned widget.
