# Chatterino Better Browser Extension

One browser extension for Firefox, Chrome, and Edge - chat overlay, pinned messages, channel points, chat identity badges, polls, and predictions in a single install.

## Installation

### Firefox

1. Install from Mozilla Add-ons (or load `dist/chatterino-browser-extension-firefox/manifest.json` via `about:debugging`)
2. Open the extension popup and click **Enable on Twitch** if Firefox prompts for Twitch site access
3. Start **Chatterino Better Browser** on your computer so the native messaging host (`com.chatterino.chatterino`) is registered

### Chrome / Edge

1. Open `chrome://extensions` or `edge://extensions`
2. Enable **Developer mode**
3. Click **Load unpacked**
4. Select `chatterino-extension` (or the extracted `chatterino-browser-extension` folder from a release)
5. Start **Chatterino Better Browser**, then restart it once after loading the extension

> **Upgrading?** Remove any old **Native Host** or **Companion** extensions - you only need this one.

## What it does

| Feature                | How                                                                              |
| ---------------------- | -------------------------------------------------------------------------------- |
| Chat overlay           | Replaces Twitch chat with Chatterino on Windows                                  |
| Window sync            | Reports channel and chat geometry to the local desktop app                       |
| Pinned messages        | Forwards moderator pins to Chatterino                                            |
| Predictions & polls    | Syncs live poll and prediction UI to the toolbar                                 |
| Channel points         | Auto-claims bonus chests and keeps points UI working when native chat is hidden  |
| Chat identity          | Badge button in the toolbar to open Twitch's Chat Identity menu                  |
| Anti-wipe              | Keeps hidden Twitch React nodes mounted so native Twitch controls stay working   |

## Popup settings

Click the extension icon:

- **Replace Twitch chat** - toggle overlay (Windows only, page reload required)
- **Channel points claimer** - auto-claim green bonus chests
- **Integration health** - native connection, active channel, chat attachment, GraphQL freshness, and current poll/prediction state

## Privacy and Native Messaging

- Communicates only with your local **Chatterino Better Browser** desktop app via `com.chatterino.chatterino` (`nativeMessaging`) and directly with Twitch (`https://*.twitch.tv/*`) using your existing Twitch session.
- Collects and transmits zero telemetry or personal data to the developer or any third party (`data_collection_permissions.required: ["none"]`).
- Allowed extension IDs built into Chatterino Better Browser:
  - Firefox: `chatterino-better-browser@trinlol.github.io`
  - Chrome / Edge: `bogfpdfoagkaebimmlcbgmfmanhbhhlm`
